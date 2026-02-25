#pragma once

#include <string>
#include <vector>

struct DiskInfo {
    std::string path;
    long total_kb;
    long used_kb;
    long free_kb;
    float usage_percent;
};

struct CpuInfo {
    float usage_percent;
    float idle_percent;
};

struct MemoryInfo {
    long total_kb;
    long used_kb;
    long free_kb;
    long available_kb;
    float usage_percent;
};

struct NetworkInterface {
    std::string name;
    long rx_bytes;
    long tx_bytes;
    long rx_packets;
    long tx_packets;
};

struct DiskIO {
    std::string name;
    long reads_completed;
    long writes_completed;
    long read_sectors;
    long write_sectors;
};

struct ThermalZone {
    std::string name;
    float temperature_celsius;
};

class HealthCollector {
public:
    HealthCollector();
    std::string getHealthJson();

private:
    std::string proc_path_;
    std::string sys_path_;

    std::vector<DiskInfo>         getDiskInfo();
    CpuInfo                       getCpuInfo();
    MemoryInfo                    getMemoryInfo();
    std::vector<NetworkInterface> getNetworkInterfaces();
    std::vector<DiskIO>           getDiskIOStats();
    std::vector<ThermalZone>      getThermalZones();
};
