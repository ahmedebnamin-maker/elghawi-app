#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

#include "analyzer.h"
#include "monitor.h"

int runAnalyzerTests();   // defined in analyzer_tests.cpp

namespace {

int currentYear() {
    std::time_t t = std::time(nullptr);
    std::tm tm = {};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm.tm_year + 1900;
}

const char* storageLabel(StorageType t) {
    switch (t) {
        case StorageType::SSD: return "SSD";
        case StorageType::HDD: return "HDD";
        default: return "unknown type";
    }
}

}  // namespace

int main(int argc, char** argv) {
    // Run the unit tests with:  SystemMonitor.exe --test
    if (argc > 1 && std::string(argv[1]) == "--test") {
        return runAnalyzerTests() == 0 ? 0 : 1;
    }

    SystemMonitor monitor;   // takes the baseline for CPU / network
    const HardwareInfo hw = SystemMonitor::readHardwareInfo();
    const Assessment a = assess(hw, currentYear());

    std::cout << std::fixed << std::setprecision(1);

    // ---------------- Device ----------------
    std::cout << "===== Your device =====\n";
    std::cout << "Type    : " << (hw.isLaptop ? "Laptop" : "Desktop") << "\n";
    std::cout << "CPU     : " << hw.cpuName << " (" << hw.logicalProcessors << " threads)\n";
    if (a.cpu.recognized) {
        std::cout << "          " << a.cpu.family << ", " << a.cpu.generation;
        if (a.cpu.releaseYear > 0) std::cout << ", released ~" << a.cpu.releaseYear;
        std::cout << "\n";
    }
    for (const GpuInfo& g : hw.gpus) {
        std::cout << "GPU     : " << g.name << " (" << g.vramGB << " GB dedicated)\n";
    }
    std::cout << "RAM     : " << nominalRamGB(hw.totalRamGB) << " GB (" << hw.totalRamGB << " GB usable)\n";
    std::cout << "Storage : C: " << hw.diskTotalGB << " GB, " << hw.diskFreeGB << " GB free, "
              << storageLabel(hw.storageType) << "\n";
    std::cout << "Screen  : " << hw.screenWidth << "x" << hw.screenHeight << " @ " << hw.refreshHz
              << " Hz (" << a.screenLabel << ")\n";

    // ---------------- Rating ----------------
    std::cout << "\n===== Rating (an estimate, not a real benchmark) =====\n";
    std::cout << "Overall : " << a.overallScore << "/100  ->  " << a.category << "\n";
    std::cout << "CPU " << a.cpuScore << " | GPU " << a.gpuScore << " | RAM " << a.ramScore
              << " | Storage " << a.storageScore << " | Screen " << a.screenScore << "\n";

    // ---------------- Advice ----------------
    std::cout << "\n===== Recommendations =====\n";
    for (const Advice& adv : a.advice) {
        std::cout << "[" << urgencyLabel(adv.urgency) << "] " << adv.component << ": " << adv.text << "\n";
    }

    // ---------------- Live ----------------
    std::cout << "\n===== Live (10 seconds) =====\n";
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const LiveStats s = monitor.readStats();
        std::cout << "CPU " << s.cpuPercent << "%  |  RAM " << s.ramPercent << "% ("
                  << s.ramUsedGB << " GB)  |  Disk " << s.diskUsedPercent << "%  |  Down "
                  << s.downKBps << " KB/s  |  Up " << s.upKBps << " KB/s\n";
    }
    return 0;
}
