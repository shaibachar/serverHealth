#include "health_collector.h"

#include "httplib.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>

static std::string readHtmlFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    // Start background thread: run internet speed test immediately, then every 1 hour
    std::thread speedThread([]() {
        HealthCollector::updateSpeedTestCache();
        while (true) {
            std::this_thread::sleep_for(std::chrono::hours(1));
            HealthCollector::updateSpeedTestCache();
        }
    });
    speedThread.detach();

    httplib::Server svr;

    // Serve the web dashboard
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        const char* webRoot = std::getenv("WEB_ROOT");
        std::string indexPath = webRoot ? std::string(webRoot) + "/index.html"
                                        : "/usr/share/serverhealth/index.html";
        std::string html = readHtmlFile(indexPath);
        if (html.empty()) {
            res.status = 404;
            res.set_content("index.html not found", "text/plain");
        } else {
            res.set_content(html, "text/html");
        }
    });

    // Health metrics JSON API
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        HealthCollector collector;
        res.set_content(collector.getHealthJson(), "application/json");
    });

    // CORS header so the page can be served from any origin during development
    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"}
    });

    int port = 9090;
    const char* portEnv = std::getenv("PORT");
    if (portEnv) port = std::stoi(portEnv);

    std::cout << "Server Health Dashboard listening on http://0.0.0.0:"
              << port << std::endl;

    svr.listen("0.0.0.0", port);
    return 0;
}
