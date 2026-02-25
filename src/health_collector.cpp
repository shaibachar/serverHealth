#include "health_collector.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/statvfs.h>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

// ---------------------------------------------------------------------------
// constructor
// ---------------------------------------------------------------------------

HealthCollector::HealthCollector() {
    const char* proc = std::getenv("PROC_PATH");
    const char* sys  = std::getenv("SYS_PATH");
    proc_path_ = proc ? proc : "/proc";
    sys_path_  = sys  ? sys  : "/sys";
}

// ---------------------------------------------------------------------------
// CPU  –  two /proc/stat snapshots 200 ms apart
// ---------------------------------------------------------------------------

CpuInfo HealthCollector::getCpuInfo() {
    auto sample = [&]() -> std::vector<long> {
        std::string line;
        std::ifstream f(proc_path_ + "/stat");
        std::getline(f, line);               // first line: "cpu ..."
        std::istringstream ss(line);
        std::string label;
        ss >> label;                          // skip "cpu"
        std::vector<long> vals;
        long v;
        while (ss >> v) vals.push_back(v);
        return vals;
    };

    auto v1 = sample();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto v2 = sample();

    CpuInfo info{};
    if (v1.size() < 4 || v2.size() < 4) return info;

    // indices: user(0) nice(1) system(2) idle(3) iowait(4) ...
    long idle1 = v1[3], idle2 = v2[3];
    long total1 = 0, total2 = 0;
    for (auto x : v1) total1 += x;
    for (auto x : v2) total2 += x;

    long d_total = total2 - total1;
    long d_idle  = idle2  - idle1;

    if (d_total > 0) {
        info.idle_percent  = 100.0f * d_idle / d_total;
        info.usage_percent = 100.0f - info.idle_percent;
    }
    return info;
}

// ---------------------------------------------------------------------------
// Memory  –  /proc/meminfo
// ---------------------------------------------------------------------------

MemoryInfo HealthCollector::getMemoryInfo() {
    MemoryInfo info{};
    std::ifstream f(proc_path_ + "/meminfo");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string key; long val; std::string unit;
        ss >> key >> val >> unit;
        if      (key == "MemTotal:")     info.total_kb     = val;
        else if (key == "MemFree:")      info.free_kb      = val;
        else if (key == "MemAvailable:") info.available_kb = val;
    }
    info.used_kb = info.total_kb - info.free_kb;
    if (info.total_kb > 0)
        info.usage_percent = 100.0f * (info.total_kb - info.available_kb) / info.total_kb;
    return info;
}

// ---------------------------------------------------------------------------
// Disk space  –  statvfs on mount points found in /proc/mounts
// ---------------------------------------------------------------------------

std::vector<DiskInfo> HealthCollector::getDiskInfo() {
    std::vector<DiskInfo> result;
    std::ifstream f(proc_path_ + "/mounts");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string dev, mount, fstype;
        ss >> dev >> mount >> fstype;
        // skip pseudo filesystems
        if (fstype == "proc" || fstype == "sysfs" || fstype == "tmpfs" ||
            fstype == "devtmpfs" || fstype == "cgroup" || fstype == "cgroup2" ||
            fstype == "devpts" || fstype == "overlay" || fstype == "none")
            continue;
        if (dev.rfind("/dev/", 0) != 0) continue;

        struct statvfs st{};
        if (statvfs(mount.c_str(), &st) != 0) continue;

        DiskInfo di;
        di.path         = mount;
        di.total_kb     = (long)((st.f_blocks * st.f_frsize) / 1024);
        di.free_kb      = (long)((st.f_bfree  * st.f_frsize) / 1024);
        di.used_kb      = di.total_kb - di.free_kb;
        di.usage_percent = (di.total_kb > 0)
                         ? 100.0f * di.used_kb / di.total_kb : 0.0f;
        result.push_back(di);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Network  –  /proc/net/dev
// ---------------------------------------------------------------------------

std::vector<NetworkInterface> HealthCollector::getNetworkInterfaces() {
    std::vector<NetworkInterface> result;
    std::ifstream f(proc_path_ + "/net/dev");
    std::string line;
    int lineno = 0;
    while (std::getline(f, line)) {
        if (++lineno <= 2) continue;   // skip two header lines
        std::istringstream ss(line);
        std::string name;
        ss >> name;
        if (!name.empty() && name.back() == ':')
            name.pop_back();
        if (name == "lo") continue;

        NetworkInterface ni;
        ni.name = name;
        long dummy;
        ss >> ni.rx_bytes >> ni.rx_packets
           >> dummy >> dummy >> dummy >> dummy >> dummy >> dummy
           >> ni.tx_bytes >> ni.tx_packets;
        result.push_back(ni);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Disk I/O  –  /proc/diskstats
// ---------------------------------------------------------------------------

std::vector<DiskIO> HealthCollector::getDiskIOStats() {
    std::vector<DiskIO> result;
    std::ifstream f(proc_path_ + "/diskstats");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        long major, minor;
        std::string name;
        ss >> major >> minor >> name;
        // skip partitions (sda1, sdb2, …) and loop/ram devices
        if (name.rfind("loop", 0) == 0 || name.rfind("ram", 0) == 0) continue;
        bool hasDigit = false;
        for (char c : name) if (std::isdigit(c)) { hasDigit = true; break; }
        if (hasDigit) continue;

        DiskIO di;
        di.name = name;
        long dummy;
        ss >> di.reads_completed >> dummy >> di.read_sectors >> dummy
           >> di.writes_completed >> dummy >> di.write_sectors;
        result.push_back(di);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Temperature  –  /sys/class/thermal/thermal_zone*/temp
// ---------------------------------------------------------------------------

std::vector<ThermalZone> HealthCollector::getThermalZones() {
    std::vector<ThermalZone> result;
    std::string base = sys_path_ + "/class/thermal";
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(base, ec)) {
        std::string dir = entry.path().string();
        if (dir.find("thermal_zone") == std::string::npos) continue;

        std::string typeFile = dir + "/type";
        std::string tempFile = dir + "/temp";

        std::ifstream tf(tempFile);
        if (!tf.is_open()) continue;

        long raw = 0;
        tf >> raw;

        std::string typeName = entry.path().filename().string();
        std::ifstream tyf(typeFile);
        if (tyf.is_open()) std::getline(tyf, typeName);

        ThermalZone tz;
        tz.name                = typeName;
        tz.temperature_celsius = raw / 1000.0f;
        result.push_back(tz);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

SpeedTestResult HealthCollector::s_speedResult;
std::mutex      HealthCollector::s_speedMutex;

// ---------------------------------------------------------------------------
// Docker containers  –  `docker ps`
// ---------------------------------------------------------------------------

// Extract a value from a tab-separated field produced by docker ps --format
static std::string extractHealth(const std::string& status) {
    // Status examples:
    //   "Up 2 hours (healthy)"
    //   "Up 3 minutes (unhealthy)"
    //   "Up 5 minutes (health: starting)"
    //   "Up 2 hours"  → no health check
    //   "Exited (0) ..."
    auto lp = status.find('(');
    if (lp == std::string::npos) return "none";
    auto rp = status.find(')', lp);
    std::string inner = status.substr(lp + 1, rp == std::string::npos ? std::string::npos : rp - lp - 1);
    if (inner == "healthy")   return "healthy";
    if (inner == "unhealthy") return "unhealthy";
    if (inner.rfind("health:", 0) == 0) return "starting";
    return "none";
}

std::vector<DockerContainer> HealthCollector::getDockerContainers() {
    std::vector<DockerContainer> result;
    // Use tab-separated format to avoid needing a JSON parser
    FILE* fp = popen(
        "docker ps --format \"{{.ID}}\\t{{.Image}}\\t{{.Names}}\\t{{.Status}}\\t{{.State}}\" 2>/dev/null",
        "r");
    if (!fp) return result;

    char buf[1024];
    while (fgets(buf, sizeof(buf), fp)) {
        std::string line(buf);
        // strip trailing newline
        if (!line.empty() && line.back() == '\n') line.pop_back();

        std::istringstream ss(line);
        DockerContainer c;
        std::getline(ss, c.id,     '\t');
        std::getline(ss, c.image,  '\t');
        std::getline(ss, c.names,  '\t');
        std::getline(ss, c.status, '\t');
        std::getline(ss, c.state,  '\t');
        c.health = extractHealth(c.status);
        if (!c.id.empty()) result.push_back(c);
    }
    pclose(fp);
    return result;
}

// ---------------------------------------------------------------------------
// Internet speed test  –  cached, refreshed every 1 h from a background thread
// Uses speedtest-cli if available, otherwise falls back to curl download test.
// ---------------------------------------------------------------------------

static std::string runCommand(const char* cmd) {
    FILE* fp = popen(cmd, "r");
    if (!fp) return "";
    char buf[256];
    std::string out;
    while (fgets(buf, sizeof(buf), fp)) out += buf;
    pclose(fp);
    // strip trailing whitespace
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    return out;
}

void HealthCollector::updateSpeedTestCache() {
    SpeedTestResult res;

    // Prefer speedtest-cli (pip install speedtest-cli)
    std::string which = runCommand("which speedtest-cli 2>/dev/null");
    if (!which.empty()) {
        // speedtest-cli --simple outputs:
        //   Ping: X ms
        //   Download: X Mbit/s
        //   Upload: X Mbit/s
        std::string out = runCommand("speedtest-cli --simple 2>/dev/null");
        std::istringstream ss(out);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.rfind("Download:", 0) == 0) {
                std::istringstream ls(line.substr(9));
                ls >> res.download_mbps;
            } else if (line.rfind("Upload:", 0) == 0) {
                std::istringstream ls(line.substr(7));
                ls >> res.upload_mbps;
            }
        }
        res.available = (res.download_mbps > 0.0f || res.upload_mbps > 0.0f);
    }

    // Fallback: curl download test against Cloudflare speed endpoint
    if (!res.available) {
        std::string out = runCommand(
            "curl -s --max-time 20 -o /dev/null "
            "-w \"%{speed_download}\" "
            "https://speed.cloudflare.com/__down?bytes=5000000 2>/dev/null");
        if (!out.empty()) {
            try {
                float bytesPerSec = std::stof(out);
                res.download_mbps = bytesPerSec * 8.0f / 1e6f;
                res.available = (res.download_mbps > 0.0f);
            } catch (...) {}
        }
    }

    // Timestamp
    std::time_t now = std::time(nullptr);
    char tsBuf[32];
    std::strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    res.timestamp = tsBuf;

    std::lock_guard<std::mutex> lock(s_speedMutex);
    s_speedResult = res;
}



// ---------------------------------------------------------------------------
// Assemble JSON
// ---------------------------------------------------------------------------

std::string HealthCollector::getHealthJson() {
    auto cpu    = getCpuInfo();
    auto mem    = getMemoryInfo();
    auto disks  = getDiskInfo();
    auto nets   = getNetworkInterfaces();
    auto ios    = getDiskIOStats();
    auto temps  = getThermalZones();
    auto dockers = getDockerContainers();

    // Snapshot cached speed result
    SpeedTestResult speed;
    {
        std::lock_guard<std::mutex> lock(s_speedMutex);
        speed = s_speedResult;
    }

    // ISO-8601 timestamp
    std::time_t now = std::time(nullptr);
    char tsBuf[32];
    std::strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));

    std::ostringstream j;
    j << "{\n";
    j << "  \"timestamp\": \"" << tsBuf << "\",\n";

    // CPU
    j << "  \"cpu\": {\n"
      << "    \"usage_percent\": " << cpu.usage_percent << ",\n"
      << "    \"idle_percent\": "  << cpu.idle_percent  << "\n"
      << "  },\n";

    // Memory
    j << "  \"memory\": {\n"
      << "    \"total_kb\": "     << mem.total_kb     << ",\n"
      << "    \"used_kb\": "      << mem.used_kb      << ",\n"
      << "    \"free_kb\": "      << mem.free_kb      << ",\n"
      << "    \"available_kb\": " << mem.available_kb << ",\n"
      << "    \"usage_percent\": "<< mem.usage_percent<< "\n"
      << "  },\n";

    // Disks
    j << "  \"disks\": [\n";
    for (size_t i = 0; i < disks.size(); ++i) {
        const auto& d = disks[i];
        j << "    {\n"
          << "      \"path\": \""          << escapeJson(d.path)   << "\",\n"
          << "      \"total_kb\": "        << d.total_kb           << ",\n"
          << "      \"used_kb\": "         << d.used_kb            << ",\n"
          << "      \"free_kb\": "         << d.free_kb            << ",\n"
          << "      \"usage_percent\": "   << d.usage_percent      << "\n"
          << "    }" << (i + 1 < disks.size() ? "," : "") << "\n";
    }
    j << "  ],\n";

    // Network
    j << "  \"network\": [\n";
    for (size_t i = 0; i < nets.size(); ++i) {
        const auto& n = nets[i];
        j << "    {\n"
          << "      \"name\": \""      << escapeJson(n.name) << "\",\n"
          << "      \"rx_bytes\": "    << n.rx_bytes         << ",\n"
          << "      \"tx_bytes\": "    << n.tx_bytes         << ",\n"
          << "      \"rx_packets\": "  << n.rx_packets       << ",\n"
          << "      \"tx_packets\": "  << n.tx_packets       << "\n"
          << "    }" << (i + 1 < nets.size() ? "," : "") << "\n";
    }
    j << "  ],\n";

    // Disk I/O
    j << "  \"disk_io\": [\n";
    for (size_t i = 0; i < ios.size(); ++i) {
        const auto& io = ios[i];
        j << "    {\n"
          << "      \"name\": \""             << escapeJson(io.name)     << "\",\n"
          << "      \"reads_completed\": "    << io.reads_completed      << ",\n"
          << "      \"writes_completed\": "   << io.writes_completed     << ",\n"
          << "      \"read_sectors\": "       << io.read_sectors         << ",\n"
          << "      \"write_sectors\": "      << io.write_sectors        << "\n"
          << "    }" << (i + 1 < ios.size() ? "," : "") << "\n";
    }
    j << "  ],\n";

    // Temperature
    j << "  \"temperature\": [\n";
    for (size_t i = 0; i < temps.size(); ++i) {
        const auto& t = temps[i];
        j << "    {\n"
          << "      \"name\": \""               << escapeJson(t.name) << "\",\n"
          << "      \"temperature_celsius\": "  << t.temperature_celsius << "\n"
          << "    }" << (i + 1 < temps.size() ? "," : "") << "\n";
    }
    j << "  ],\n";

    // Docker containers
    j << "  \"docker\": [\n";
    for (size_t i = 0; i < dockers.size(); ++i) {
        const auto& c = dockers[i];
        j << "    {\n"
          << "      \"id\": \""     << escapeJson(c.id)     << "\",\n"
          << "      \"image\": \""  << escapeJson(c.image)  << "\",\n"
          << "      \"names\": \""  << escapeJson(c.names)  << "\",\n"
          << "      \"status\": \"" << escapeJson(c.status) << "\",\n"
          << "      \"state\": \""  << escapeJson(c.state)  << "\",\n"
          << "      \"health\": \"" << escapeJson(c.health) << "\"\n"
          << "    }" << (i + 1 < dockers.size() ? "," : "") << "\n";
    }
    j << "  ],\n";

    // Internet speed (cached)
    j << "  \"internet_speed\": {\n"
      << "    \"available\": "       << (speed.available ? "true" : "false") << ",\n"
      << "    \"download_mbps\": "   << speed.download_mbps                  << ",\n"
      << "    \"upload_mbps\": "     << speed.upload_mbps                    << ",\n"
      << "    \"last_checked\": \""  << escapeJson(speed.timestamp)          << "\"\n"
      << "  }\n";

    j << "}\n";
    return j.str();
}
