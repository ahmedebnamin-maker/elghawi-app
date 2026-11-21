#pragma once
// Plain data structures shared by the monitor (Windows-only code) and the
// analyzer (pure C++ logic). No Windows headers here on purpose.

#include <string>
#include <vector>

enum class StorageType { Unknown, SSD, HDD };

struct GpuInfo {
    std::string name;
    double vramGB = 0.0;   // dedicated video memory
};

// Static information: read once at startup.
struct HardwareInfo {
    std::string cpuName;
    int logicalProcessors = 0;
    double totalRamGB = 0.0;
    double diskTotalGB = 0.0;       // system drive (C:)
    double diskFreeGB = 0.0;
    StorageType storageType = StorageType::Unknown;
    int screenWidth = 0;
    int screenHeight = 0;
    int refreshHz = 0;
    bool isLaptop = false;
    std::vector<GpuInfo> gpus;
};

// Live information: read every second.
struct LiveStats {
    double cpuPercent = 0.0;
    double ramPercent = 0.0;
    double ramUsedGB = 0.0;
    double diskUsedPercent = 0.0;
    double downKBps = 0.0;
    double upKBps = 0.0;
};
