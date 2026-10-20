#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>

#include "crow.h"
#include "request_params.hpp"
#include "route_provider.hpp"
#include "../traffic/lru_cache.hpp"
#include "../traffic/rate_limiter.hpp"

namespace {

// Directory holding index.html, style.css, app.js (override with ROUTELENS_WEB_DIR).
std::string webDir() {
    const char* d = std::getenv("ROUTELENS_WEB_DIR");
    return d ? d : "web";
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

const char* contentType(const std::string& name) {
    auto ends = [&](const char* ext) {
        const std::string e(ext);
        return name.size() >= e.size() &&
               name.compare(name.size() - e.size(), e.size(), e) == 0;
    };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".js")) return "application/javascript; charset=utf-8";
    if (ends(".svg")) return "image/svg+xml";
    return "application/octet-stream";
}

crow::response serveFile(const std::string& name) {
    std::string body;
    if (name.find("..") != std::string::npos || !readFile(webDir() + "/" + name, body)) {
        return crow::response(404);
    }
    crow::response res(200, body);
    res.set_header("Content-Type", contentType(name));
    return res;
}

crow::response jsonResponse(int code, const crow::json::wvalue& body) {
    crow::response res(code, body.dump());
    res.set_header("Content-Type", "application/json");
    return res;
}

crow::response jsonError(int code, const std::string& message) {
    crow::json::wvalue body;
    body["error"] = message;
    return jsonResponse(code, body);
}

}  // namespace

int main() {
    crow::SimpleApp app;

    // Shared state. LRUCache is not thread-safe and Crow is multithreaded,
    // so every cache access below is guarded by cacheMutex.
    rl::RateLimiter trafficLimiter(5, std::chrono::seconds(1));
    rl::LRUCache<std::string, double> trafficCache(1024);
    std::mutex cacheMutex;
    std::unique_ptr<rl::RouteProvider> provider = std::make_unique<rl::NullRouteProvider>();
    const bool trafficKeyConfigured = std::getenv("TOMTOM_API_KEY") != nullptr;

    CROW_ROUTE(app, "/api/health")
    ([] {
        crow::json::wvalue response;
        response["status"] = "ok";
        response["service"] = "RouteLens API";
        return response;
    });

    CROW_ROUTE(app, "/api/traffic/status")
    ([&] {
        crow::json::wvalue response;
        response["source"] = trafficKeyConfigured ? "live" : "free_flow_fallback";
        response["api_key_configured"] = trafficKeyConfigured;
        response["rate_limit"]["max_requests"] = 5;
        response["rate_limit"]["window_seconds"] = 1;
        response["rate_limit"]["current_count"] = trafficLimiter.current_count();
        std::lock_guard<std::mutex> lock(cacheMutex);
        response["cache"]["size"] = trafficCache.size();
        response["cache"]["capacity"] = trafficCache.capacity();
        return response;
    });

    CROW_ROUTE(app, "/api/routes")
    ([&](const crow::request& req) {
        rl::ParamGetter get = [&](const std::string& key) -> std::optional<std::string> {
            const char* v = req.url_params.get(key);
            if (!v) {
                return std::nullopt;
            }
            return std::string(v);
        };
        std::string err;
        auto parsed = rl::parseRouteRequest(get, err);
        if (!parsed) {
            return jsonError(400, err);
        }
        if (!provider->available()) {
            return jsonError(501, "routing engine not connected yet");
        }

        crow::json::wvalue out;
        out["scenario"] = parsed->scenario;
        std::size_t i = 0;
        for (const auto& r : provider->compute(*parsed)) {
            auto& item = out["routes"][i++];
            item["name"] = r.name;
            item["eta_min"] = r.etaMinutes;
            item["distance_km"] = r.distanceKm;
            item["signals"] = r.signals;
            item["traffic"] = r.trafficLevel;
            item["score"] = r.score;
            item["recommended"] = r.recommended;
            std::size_t j = 0;
            for (const auto& p : r.polyline) {
                item["polyline"][j][0] = p.first;
                item["polyline"][j][1] = p.second;
                ++j;
            }
        }
        return jsonResponse(200, out);
    });

    CROW_ROUTE(app, "/")([] { return serveFile("index.html"); });
    CROW_ROUTE(app, "/web/<string>")([](const std::string& name) { return serveFile(name); });

    app.port(18080)
       .bindaddr("127.0.0.1")
       .multithreaded()
       .run();
}
