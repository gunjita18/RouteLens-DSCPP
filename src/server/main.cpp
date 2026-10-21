#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>

#include "crow.h"
#include "preview_provider.hpp"
#include "request_params.hpp"
#include "route_cache.hpp"
#include "route_provider.hpp"
#include "../data/http_client.hpp"
#include "../data/osm_loader.hpp"
#include "../spatial/road_snapper.hpp"
#include "../traffic/lru_cache.hpp"
#include "../traffic/rate_limiter.hpp"
#include "../traffic/tomtom_client.hpp"

namespace {

// Directory holding index.html, style.css, app.js (override with ROUTELENS_WEB_DIR).
std::string webDir() {
    const char* d = std::getenv("ROUTELENS_WEB_DIR");
    return d ? d : "web";
}

// Directory holding nodes.csv and edges.csv (override with ROUTELENS_DATA_DIR).
std::string dataDir() {
    const char* d = std::getenv("ROUTELENS_DATA_DIR");
    return d ? d : "data/osm";
}

// Directory for the TomTom daily-usage counter (override with ROUTELENS_CACHE_DIR).
// It must be writable; in Docker mount a volume there to keep the count across restarts.
std::string cacheDir() {
    const char* d = std::getenv("ROUTELENS_CACHE_DIR");
    return d ? d : "data/cache";
}

// A click farther than this from any road is rejected.
constexpr double kMaxSnapMeters = 500.0;

// Bind address and port. Defaults keep the server local-only; the Docker image
// sets ROUTELENS_BIND=0.0.0.0 so the container port can be published.
std::string bindAddress() {
    const char* a = std::getenv("ROUTELENS_BIND");
    return a ? a : "127.0.0.1";
}

std::uint16_t listenPort() {
    const char* p = std::getenv("ROUTELENS_PORT");
    if (p) {
        const long v = std::strtol(p, nullptr, 10);
        if (v > 0 && v < 65536) {
            return static_cast<std::uint16_t>(v);
        }
    }
    return 18080;
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

    // Protects /api/routes (and so the TomTom quota behind it) from request
    // floods: more than 10 requests in one second get HTTP 429.
    rl::RateLimiter routeLimiter(10, std::chrono::seconds(1));
    std::atomic<int> rejectedRequests{0};
    // Route results are cached for 5 minutes (same TTL as the traffic client), so
    // repeating a query neither recomputes routes nor spends TomTom requests.
    rl::RouteCache routeCache(256, 300.0);

    // The routing engine plugs in here. Until it exists the server answers 501.
    // ROUTELENS_PREVIEW=1 switches on SAMPLE routes for showing the interface; they
    // are not computed from the road network and are labelled as such everywhere.
    std::unique_ptr<rl::RouteProvider> provider = std::make_unique<rl::NullRouteProvider>();
    const char* previewEnv = std::getenv("ROUTELENS_PREVIEW");
    if (previewEnv != nullptr && std::string(previewEnv) == "1") {
        provider = std::make_unique<rl::PreviewRouteProvider>();
        CROW_LOG_WARNING << "PREVIEW MODE: /api/routes returns SAMPLE routes, not real ones";
    }

    // TomTom live traffic (client owned by the traffic module). The key is read
    // only from the environment, goes only into the request URL inside the
    // client, and is never logged or sent to the browser. Without a key the
    // client disables itself and the server reports free-flow data.
    // The routing engine feeds it through rl::updateRouteTraffic().
    const char* keyEnv = std::getenv("TOMTOM_API_KEY");
    const bool trafficKeyConfigured = keyEnv != nullptr && keyEnv[0] != '\0';
    rl::HttpClient http("RouteLens/0.1 (Graphic Era PBL project)", 10);
    rl::TomTomOptions ttOptions;
    if (trafficKeyConfigured) {
        ttOptions.apiKey = keyEnv;
    }
    ttOptions.usageFile = cacheDir() + "/tomtom_usage.txt";
    rl::TomTomClient tomtom(ttOptions, [&http](const std::string& url) { return http.get(url); });
    std::mutex tomtomMutex;  // TomTomClient is not thread-safe and Crow is multithreaded
    bool trafficUsable = trafficKeyConfigured;
    std::string selfTestStatus = "not_run";

    // Opt-in check that the key works: one real request (counts toward the daily
    // quota). Run with ROUTELENS_TRAFFIC_SELFTEST=1; the result is logged and
    // shown in /api/traffic/status. Only the status is logged, never the URL.
    if (std::getenv("ROUTELENS_TRAFFIC_SELFTEST") != nullptr) {
        const rl::FetchResult r = tomtom.fetch({30.3243, 78.0412});  // Clock Tower
        selfTestStatus = rl::fetchStatusName(r.status);
        trafficUsable = rl::hasSegment(r.status) || r.status == rl::FetchStatus::NoCoverage ||
                        r.status == rl::FetchStatus::CachedNoCoverage;
        CROW_LOG_INFO << "TomTom self-test: " << selfTestStatus;
    }

    // Road network (owned by the map-data module) and the KD-tree that snaps
    // clicks onto it. Without the CSVs the server still runs; /api/routes then
    // answers 503.
    rl::OsmNetwork network;
    std::unique_ptr<rl::RoadSnapper> snapper;
    {
        std::string loadErr;
        const std::string dir = dataDir();
        if (rl::loadOsmNetwork(dir, network, loadErr)) {
            snapper = std::make_unique<rl::RoadSnapper>(network);
            CROW_LOG_INFO << "road network loaded from " << dir << ": " << network.stats.keptNodes
                          << " nodes, " << network.stats.keptEdges << " edges, "
                          << snapper->pointCount() << " KD-tree points";
        } else {
            CROW_LOG_WARNING << "road network NOT loaded from " << dir << ": " << loadErr;
        }
    }

    CROW_ROUTE(app, "/api/health")
    ([&] {
        crow::json::wvalue response;
        response["status"] = "ok";
        response["service"] = "RouteLens API";
        response["preview"] = provider->isPreview();
        response["network"]["loaded"] = snapper != nullptr;
        if (snapper) {
            response["network"]["nodes"] = network.stats.keptNodes;
            response["network"]["edges"] = network.stats.keptEdges;
        }
        return response;
    });

    CROW_ROUTE(app, "/api/traffic/status")
    ([&] {
        crow::json::wvalue response;
        response["source"] = trafficUsable ? "live" : "free_flow_fallback";
        response["api_key_configured"] = trafficKeyConfigured;
        // Limit on incoming /api/routes requests.
        response["rate_limit"]["max_requests"] = 10;
        response["rate_limit"]["window_seconds"] = 1;
        response["rate_limit"]["current_count"] = routeLimiter.current_count();
        response["rate_limit"]["rejected_total"] = rejectedRequests.load();
        {
            std::lock_guard<std::mutex> lock(tomtomMutex);
            response["tomtom"]["requests_today"] = tomtom.requestsToday();
            response["tomtom"]["requests_this_run"] = tomtom.requestsThisRun();
            response["tomtom"]["cache_hits"] = tomtom.cacheHits();
            response["tomtom"]["daily_cap"] = ttOptions.dailyCap;
        }
        response["tomtom"]["self_test"] = selfTestStatus;
        response["route_cache"]["size"] = routeCache.size();
        response["route_cache"]["capacity"] = routeCache.capacity();
        response["route_cache"]["hits"] = routeCache.hits();
        response["route_cache"]["misses"] = routeCache.misses();
        response["route_cache"]["ttl_seconds"] = routeCache.ttlSeconds();
        return response;
    });

    CROW_ROUTE(app, "/api/routes")
    ([&](const crow::request& req) {
        if (!routeLimiter.allow_request()) {
            ++rejectedRequests;
            crow::response tooMany = jsonError(429, "too many requests, try again in a moment");
            tooMany.set_header("Retry-After", "1");
            return tooMany;
        }
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
        if (!snapper) {
            return jsonError(503, "road network not loaded on the server");
        }

        const rl::SnapResult from = snapper->snap({parsed->fromLat, parsed->fromLon}, kMaxSnapMeters);
        const rl::SnapResult to = snapper->snap({parsed->toLat, parsed->toLon}, kMaxSnapMeters);
        if (!from.found) {
            return jsonError(422, "no road within 500 m of the start point");
        }
        if (!to.found) {
            return jsonError(422, "no road within 500 m of the end point");
        }
        if (from.node == to.node) {
            return jsonError(422, "start and end snap to the same junction; pick points farther apart");
        }
        parsed->fromNode = from.node;
        parsed->toNode = to.node;

        // Where each click landed on the road network, shown on the website.
        auto addSnapped = [&](crow::json::wvalue& body) {
            auto put = [&](const char* key, const rl::SnapResult& s) {
                body["snapped"][key]["node"] = s.node;
                body["snapped"][key]["lat"] = s.nodePos.lat;
                body["snapped"][key]["lon"] = s.nodePos.lon;
                body["snapped"][key]["road_class"] = s.roadClass;
                body["snapped"][key]["road_distance_m"] = s.roadDistanceM;
            };
            put("from", from);
            put("to", to);
        };

        if (!provider->available()) {
            crow::json::wvalue body;
            body["error"] = "routing engine not connected yet";
            addSnapped(body);
            return jsonResponse(501, body);
        }

        // Same query within 5 minutes: answer from the cache, no recomputation.
        const std::string cacheKey = rl::RouteCache::makeKey(*parsed);
        std::optional<std::vector<rl::RouteInfo>> routes = routeCache.get(cacheKey);
        const bool fromCache = routes.has_value();
        if (!routes) {
            routes = provider->compute(*parsed);
            if (!routes->empty()) {
                routeCache.put(cacheKey, *routes);
            }
        }

        crow::json::wvalue out;
        out["scenario"] = parsed->scenario;
        out["preview"] = provider->isPreview();   // true = sample routes, not real
        out["from_cache"] = fromCache;
        addSnapped(out);
        std::size_t i = 0;
        for (const auto& r : *routes) {
            auto& item = out["routes"][i++];
            item["name"] = r.name;
            item["eta_s"] = r.etaSeconds;
            item["free_flow_eta_s"] = r.freeFlowEtaSeconds;
            item["distance_km"] = r.distanceKm;
            item["junctions"] = r.junctions;
            item["coverage"]["live_pct"] = r.livePct;
            item["coverage"]["typical_pct"] = r.typicalPct;
            item["coverage"]["estimated_pct"] = r.estimatedPct;
            item["traffic"] = r.trafficLevel;
            item["score"] = r.score;
            item["recommended"] = r.recommended;
            if (!r.explanation.empty()) {
                item["explanation"] = r.explanation;
            }
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

    app.port(listenPort())
       .bindaddr(bindAddress())
       .multithreaded()
       .run();
}
