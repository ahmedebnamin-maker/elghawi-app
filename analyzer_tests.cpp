// Unit tests for analyzer.cpp. Run the program with the argument --test.
#include <iostream>
#include <string>

#include "analyzer.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "FAIL: " << what << "\n";
    }
}

bool hasAdvice(const Assessment& a, const char* component, Urgency u) {
    for (const Advice& x : a.advice) {
        if (x.component == component && x.urgency == u) return true;
    }
    return false;
}

HardwareInfo makeInfo(const char* cpu, double ramGB, double diskTotal, double diskFree,
                      StorageType st, int w, int h, int hz, bool laptop,
                      const char* gpuName, double vram) {
    HardwareInfo hw;
    hw.cpuName = cpu;
    hw.logicalProcessors = 8;
    hw.totalRamGB = ramGB;
    hw.diskTotalGB = diskTotal;
    hw.diskFreeGB = diskFree;
    hw.storageType = st;
    hw.screenWidth = w;
    hw.screenHeight = h;
    hw.refreshHz = hz;
    hw.isLaptop = laptop;
    hw.gpus.push_back({gpuName, vram});
    return hw;
}

void testCpuParsing() {
    CpuProfile p = parseCpuName("13th Gen Intel(R) Core(TM) i3-1315U");
    check(p.recognized && p.vendor == "Intel" && p.tier == 3, "i3-1315U basic fields");
    check(p.generation == "13th Gen" && p.releaseYear == 2022 && p.suffix == "U", "i3-1315U gen/year/suffix");

    p = parseCpuName("Intel(R) Core(TM) i7-4770 CPU @ 3.40GHz");
    check(p.generation == "4th Gen" && p.releaseYear == 2013 && p.suffix.empty(), "i7-4770 (4 digits, no prefix)");

    p = parseCpuName("11th Gen Intel(R) Core(TM) i5-1135G7 @ 2.40GHz");
    check(p.generation == "11th Gen" && p.suffix == "G7" && p.releaseYear == 2020, "i5-1135G7");

    p = parseCpuName("Intel(R) Core(TM) i5-10210U CPU @ 1.60GHz");
    check(p.generation == "10th Gen" && p.suffix == "U", "i5-10210U (5 digits, no prefix)");

    p = parseCpuName("Intel(R) Core(TM) i7-1165G7");
    check(p.generation == "11th Gen", "i7-1165G7 without prefix is 11th gen, not 1st");

    p = parseCpuName("Intel(R) Core(TM) i7-920 CPU");
    check(p.generation == "1st Gen", "i7-920 is 1st gen");

    p = parseCpuName("12th Gen Intel(R) Core(TM) i7-12700H");
    check(p.suffix == "H" && p.generation == "12th Gen", "i7-12700H");

    p = parseCpuName("AMD Ryzen 5 5600X 6-Core Processor");
    check(p.recognized && p.vendor == "AMD" && p.tier == 5 && p.suffix == "X", "Ryzen 5 5600X");
    check(p.generation == "Ryzen 5000 series" && p.releaseYear == 2021, "Ryzen 5600X series/year");

    p = parseCpuName("AMD Ryzen 7 7840HS w/ Radeon 780M Graphics");
    check(p.tier == 7 && p.suffix == "HS", "Ryzen 7 7840HS");

    p = parseCpuName("Intel(R) Core(TM) Ultra 7 155H");
    check(p.recognized && p.family == "Core Ultra 7" && p.suffix == "H" && p.releaseYear == 2023, "Core Ultra 7 155H");

    p = parseCpuName("AMD Ryzen AI 9 HX 370 w/ Radeon 890M");
    check(p.recognized && p.tier == 9 && p.family == "Ryzen AI 9", "Ryzen AI 9");

    p = parseCpuName("Intel(R) Pentium(R) Gold G5400 CPU @ 3.70GHz");
    check(!p.recognized && p.vendor == "Intel", "Pentium is not recognized but vendor is known");

    p = parseCpuName("");
    check(!p.recognized && p.vendor == "Unknown", "empty name does not crash");
}

void testRamAndGpu() {
    check(nominalRamGB(7.71783) == 8, "7.7 GB reported -> 8");
    check(nominalRamGB(15.8) == 16, "15.8 GB reported -> 16");
    check(nominalRamGB(3.9) == 4, "3.9 GB reported -> 4");
    check(nominalRamGB(31.7) == 32, "31.7 GB reported -> 32");

    GpuProfile g = lookupGpu("NVIDIA GeForce RTX 3060 Ti");
    check(g.discrete && g.score == 68, "3060 Ti matches before 3060");

    g = lookupGpu("NVIDIA GeForce RTX 3060 Laptop GPU");
    check(g.discrete && g.score == 49, "3060 laptop gets a penalty");

    g = lookupGpu("AMD Radeon RX 5700 XT");
    check(g.score == 56, "rx 5700 does not match rx 570");

    g = lookupGpu("Intel(R) UHD Graphics");
    check(!g.discrete && g.score == 10, "UHD is integrated");

    g = lookupGpu("Intel(R) Arc(TM) Graphics");
    check(!g.discrete && g.score == 28, "Arc Graphics is integrated");

    g = lookupGpu("Intel(R) Arc(TM) A770 Graphics");
    check(g.discrete && g.score == 60, "Arc A770 is discrete");

    g = lookupGpu("AMD Radeon(TM) 780M Graphics");
    check(!g.discrete && g.score == 32, "Radeon 780M is integrated");

    g = lookupGpu("NVIDIA Some Future Card");
    check(g.discrete && !g.known && g.score == 45, "unknown NVIDIA card falls back to a discrete guess");

    g = lookupGpu("Totally Unknown Adapter");
    check(!g.discrete && g.score == 12, "unknown adapter falls back to integrated guess");
}

void testAssessment() {
    // Ahmed's own laptop (numbers from his screenshot, SSD assumed).
    HardwareInfo hw = makeInfo("13th Gen Intel(R) Core(TM) i3-1315U", 7.71783, 237.149, 42.0495,
                               StorageType::SSD, 1920, 1080, 60, true, "Intel(R) UHD Graphics", 0.125);
    Assessment a = assess(hw, 2026);
    check(a.cpuScore == 23, "laptop: cpu score 23");
    check(a.overallScore >= 25 && a.overallScore <= 30, "laptop: overall about 27");
    check(a.category == "Basic office use", "laptop: basic office");
    check(!a.hasDiscreteGpu, "laptop: no discrete GPU");
    check(hasAdvice(a, "RAM", Urgency::Soon), "laptop: RAM advice is SOON");
    check(hasAdvice(a, "Storage", Urgency::Soon), "laptop: low free space is SOON (17.7% free)");
    check(hasAdvice(a, "CPU", Urgency::Ok), "laptop: CPU still OK");

    // A typical mid-range gaming desktop.
    hw = makeInfo("AMD Ryzen 5 5600X 6-Core Processor", 15.8, 931.0, 500.0, StorageType::SSD,
                  1920, 1080, 144, false, "NVIDIA GeForce RTX 3060", 12.0);
    a = assess(hw, 2026);
    check(a.hasDiscreteGpu && a.gpuScore == 57, "gaming: RTX 3060 detected");
    check(a.category == "Gaming", "gaming: category Gaming");
    check(hasAdvice(a, "RAM", Urgency::Ok), "gaming: 16 GB RAM is OK");

    // Strong CPU, 64 GB RAM, 4K screen but NO discrete GPU: capped.
    hw = makeInfo("Intel(R) Core(TM) i9-14900K", 63.7, 1800.0, 900.0, StorageType::SSD,
                  3840, 2160, 60, false, "Intel(R) UHD Graphics 770", 0.125);
    a = assess(hw, 2026);
    check(a.overallScore >= 55, "no-GPU: overall would reach gaming level");
    check(a.category == "Strong office / light gaming", "no-GPU: category is capped");

    // Old machine with an HDD, 4 GB RAM and almost no free space.
    hw = makeInfo("Intel(R) Core(TM) i5-4590 CPU @ 3.30GHz", 3.9, 465.0, 30.0, StorageType::HDD,
                  1366, 768, 60, false, "Intel(R) HD Graphics 4600", 0.1);
    a = assess(hw, 2026);
    check(hasAdvice(a, "Storage", Urgency::Now), "old: HDD or full disk -> NOW");
    check(hasAdvice(a, "RAM", Urgency::Now), "old: 4 GB RAM -> NOW");
    check(hasAdvice(a, "CPU", Urgency::Now), "old: 13-year-old CPU -> NOW");
    check(a.category == "Basic office use", "old: basic office");

    // No GPU found at all must not crash.
    hw = makeInfo("Weird CPU", 0.0, 0.0, 0.0, StorageType::Unknown, 0, 0, 0, false, "x", 0.0);
    hw.gpus.clear();
    a = assess(hw, 2026);
    check(a.primaryGpu == "Unknown" && a.cpuScore == 40, "empty hardware info does not crash");
}

}  // namespace

// Returns the number of failed checks (0 = everything passed).
int runAnalyzerTests() {
    g_checks = 0;
    g_failures = 0;
    testCpuParsing();
    testRamAndGpu();
    testAssessment();
    std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures;
}
