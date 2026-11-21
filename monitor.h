#pragma once
#include <chrono>

#include "hardware_types.h"

// NOTE: no Windows headers in this file on purpose. They live in monitor.cpp,
// so that including this header next to httplib.h (which pulls in winsock)
// in the server phase does not cause "redefinition" errors.

class SystemMonitor {
public:
    SystemMonitor();                          // takes the first baseline reading

    static HardwareInfo readHardwareInfo();   // one-time static info
    LiveStats readStats();                    // call about once per second

private:
    struct CpuTimes {
        unsigned long long idle = 0;
        unsigned long long total = 0;
    };
    struct NetTotals {
        unsigned long long rx = 0;   // bytes received
        unsigned long long tx = 0;   // bytes sent
    };

    static bool readCpuTimes(CpuTimes& out);
    static bool readNetTotals(NetTotals& out);
    static double cpuUsagePercent(const CpuTimes& before, const CpuTimes& now);

    CpuTimes prevCpu_;
    NetTotals prevNet_;
    std::chrono::steady_clock::time_point prevTime_;
    bool cpuBaselineOk_ = false;
    bool netBaselineOk_ = false;
};
