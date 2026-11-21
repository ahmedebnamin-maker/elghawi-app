#define NOMINMAX
#include <winsock2.h>   // must come BEFORE windows.h
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <dxgi.h>
#include <intrin.h>
#include <winioctl.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

#include "monitor.h"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "dxgi.lib")

namespace {

constexpr double kGB = 1024.0 * 1024.0 * 1024.0;

unsigned long long toU64(const FILETIME& ft) {
    return (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

// Safe difference: unsigned "now - before" would wrap to a huge number if the
// counter ever went backwards. We return 0 instead.
unsigned long long safeDelta(unsigned long long now, unsigned long long before) {
    return now >= before ? now - before : 0;
}

// Wide string (UTF-16) -> UTF-8, so names with non-English characters are safe.
std::string toUtf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return "";
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::string getCpuName() {
    int regs[4] = {};
    __cpuid(regs, 0x80000000);
    if (static_cast<unsigned>(regs[0]) < 0x80000004u) return "Unknown";

    char brand[49] = {};
    for (int i = 0; i < 3; ++i) {
        __cpuid(regs, 0x80000002 + i);
        std::memcpy(brand + i * 16, regs, 16);
    }
    std::string name(brand);
    size_t start = name.find_first_not_of(' ');
    return start == std::string::npos ? "Unknown" : name.substr(start);
}

// Asks Windows whether the drive "seeks" (spinning HDD) or not (SSD).
// Opening the drive with access 0 only allows queries, so no admin rights.
StorageType detectStorageType(char driveLetter) {
    char path[] = "\\\\.\\X:";
    path[4] = driveLetter;

    HANDLE h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return StorageType::Unknown;

    STORAGE_PROPERTY_QUERY query = {};
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;

    DEVICE_SEEK_PENALTY_DESCRIPTOR result = {};
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                              &result, sizeof(result), &returned, nullptr);
    CloseHandle(h);   // always close the handle, success or not

    if (!ok) return StorageType::Unknown;
    return result.IncursSeekPenalty ? StorageType::HDD : StorageType::SSD;
}

}  // namespace

// ---------------------------------------------------------------------------
// Static hardware information
// ---------------------------------------------------------------------------
HardwareInfo SystemMonitor::readHardwareInfo() {
    HardwareInfo hw;

    hw.cpuName = getCpuName();

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    hw.logicalProcessors = static_cast<int>(si.dwNumberOfProcessors);

    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        hw.totalRamGB = static_cast<double>(mem.ullTotalPhys) / kGB;
    }

    std::error_code ec;
    auto info = std::filesystem::space("C:\\", ec);
    if (!ec) {
        hw.diskTotalGB = static_cast<double>(info.capacity) / kGB;
        hw.diskFreeGB = static_cast<double>(info.available) / kGB;
    }
    hw.storageType = detectStorageType('C');

    DEVMODEA dm = {};
    dm.dmSize = static_cast<WORD>(sizeof(dm));
    if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
        hw.screenWidth = static_cast<int>(dm.dmPelsWidth);
        hw.screenHeight = static_cast<int>(dm.dmPelsHeight);
        hw.refreshHz = static_cast<int>(dm.dmDisplayFrequency);
    }

    // BatteryFlag 128 = "no system battery" (desktop), 255 = unknown.
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        hw.isLaptop = (sps.BatteryFlag != 128 && sps.BatteryFlag != 255);
    }

    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                     reinterpret_cast<void**>(&factory)))) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; SUCCEEDED(factory->EnumAdapters1(i, &adapter)); ++i) {
            DXGI_ADAPTER_DESC1 desc;
            if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
                !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                GpuInfo g;
                g.name = toUtf8(desc.Description);
                g.vramGB = static_cast<double>(desc.DedicatedVideoMemory) / kGB;
                hw.gpus.push_back(g);
            }
            adapter->Release();
        }
        factory->Release();
    }
    return hw;
}

// ---------------------------------------------------------------------------
// Live readings
// ---------------------------------------------------------------------------
bool SystemMonitor::readCpuTimes(CpuTimes& out) {
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return false;
    out.idle = toU64(idle);
    out.total = toU64(kernel) + toU64(user);   // kernel time already includes idle
    return true;
}

double SystemMonitor::cpuUsagePercent(const CpuTimes& before, const CpuTimes& now) {
    unsigned long long idleDelta = safeDelta(now.idle, before.idle);
    unsigned long long totalDelta = safeDelta(now.total, before.total);
    if (totalDelta == 0) return 0.0;   // avoid division by zero
    double usage = (1.0 - static_cast<double>(idleDelta) / static_cast<double>(totalDelta)) * 100.0;
    return std::clamp(usage, 0.0, 100.0);
}

bool SystemMonitor::readNetTotals(NetTotals& out) {
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR) return false;

    out = NetTotals{};
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& row = table->Table[i];
        if (row.OperStatus != IfOperStatusUp) continue;                    // adapter is up
        if (!row.InterfaceAndOperStatusFlags.HardwareInterface) continue;  // real adapters only
        out.rx += row.InOctets;
        out.tx += row.OutOctets;
    }
    FreeMibTable(table);   // Windows allocated it, we must free it
    return true;
}

SystemMonitor::SystemMonitor() {
    cpuBaselineOk_ = readCpuTimes(prevCpu_);
    netBaselineOk_ = readNetTotals(prevNet_);
    prevTime_ = std::chrono::steady_clock::now();
}

LiveStats SystemMonitor::readStats() {
    LiveStats s;

    // RAM and disk are instant readings (no baseline needed).
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        s.ramPercent = static_cast<double>(mem.dwMemoryLoad);
        s.ramUsedGB = static_cast<double>(mem.ullTotalPhys - mem.ullAvailPhys) / kGB;
    }

    std::error_code ec;
    auto info = std::filesystem::space("C:\\", ec);
    if (!ec && info.capacity > 0) {
        s.diskUsedPercent =
            (1.0 - static_cast<double>(info.available) / static_cast<double>(info.capacity)) * 100.0;
    }

    // CPU needs two readings: compare with the previous one.
    CpuTimes cpu;
    if (readCpuTimes(cpu)) {
        if (cpuBaselineOk_) s.cpuPercent = cpuUsagePercent(prevCpu_, cpu);
        prevCpu_ = cpu;
        cpuBaselineOk_ = true;
    }

    // Network speed = bytes difference / REAL elapsed seconds (not a fixed 1.0).
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - prevTime_).count();

    NetTotals net;
    if (readNetTotals(net)) {
        if (!netBaselineOk_) {
            prevNet_ = net;
            prevTime_ = now;
            netBaselineOk_ = true;
        } else if (seconds >= 0.05) {
            s.downKBps = static_cast<double>(safeDelta(net.rx, prevNet_.rx)) / 1024.0 / seconds;
            s.upKBps = static_cast<double>(safeDelta(net.tx, prevNet_.tx)) / 1024.0 / seconds;
            prevNet_ = net;
            prevTime_ = now;
        }
    }
    return s;
}
