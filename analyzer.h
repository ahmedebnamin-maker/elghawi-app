#pragma once
// Pure C++ logic (no Windows code): parses the CPU name, looks up the GPU,
// scores the device, and produces upgrade advice. Because it has no OS calls
// it can be unit-tested on any machine.

#include <string>
#include <vector>

#include "hardware_types.h"

struct CpuProfile {
    bool recognized = false;
    std::string vendor = "Unknown";   // Intel / AMD / Unknown
    std::string family;               // "Core i3", "Ryzen 5", "Core Ultra 7"
    std::string generation;           // "13th Gen", "Ryzen 5000 series"
    int tier = 0;                     // 3 / 5 / 7 / 9 (0 = unknown)
    int releaseYear = 0;              // approximate, 0 = unknown
    std::string suffix;               // "U", "H", "HX", "K", "G7" ...
};

struct GpuProfile {
    bool known = false;               // found in our lookup table
    bool discrete = false;            // dedicated card (not integrated)
    int score = 0;                    // 0-100, rough relative performance
    int year = 0;                     // approximate launch year (0 = unknown)
};

enum class Urgency { Ok, Soon, Now };

struct Advice {
    std::string component;            // "CPU", "GPU", "RAM", "Storage", "Screen"
    Urgency urgency = Urgency::Ok;
    std::string text;
};

struct Assessment {
    CpuProfile cpu;
    int cpuScore = 0;
    int gpuScore = 0;
    int ramScore = 0;
    int storageScore = 0;
    int screenScore = 0;
    int overallScore = 0;             // 0-100
    std::string category;             // "Basic office use", "Gaming", ...
    std::string primaryGpu;
    bool hasDiscreteGpu = false;
    std::string screenLabel;          // "Full HD", "QHD", "4K" ...
    std::vector<Advice> advice;
};

CpuProfile parseCpuName(const std::string& cpuName);
GpuProfile lookupGpu(const std::string& gpuName);
int nominalRamGB(double reportedGB);   // 7.7 -> 8, 15.8 -> 16 ...
Assessment assess(const HardwareInfo& hw, int currentYear);
const char* urgencyLabel(Urgency u);
