#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
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
#include "osm_route_provider.hpp"
#include "../algorithms/scenario.hpp"
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

// Writes the route cards and map polylines shared by /api/routes and /api/scenario.
void putRoutes(crow::json::wvalue& out, const std::vector<rl::RouteInfo>& routes) {
    std::size_t i = 0;
    for (const auto& r : routes) {
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
        std::size_t j = 0;
        for (const auto& p : r.polyline) {
            item["polyline"][j][0] = p.first;
            item["polyline"][j][1] = p.second;
            ++j;
        }
    }
}

constexpr std::size_t kMaxClosures = 20;
constexpr double kMinClosureRadiusM = 20.0;
constexpr double kMaxClosureRadiusM = 2000.0;
constexpr double kDefaultClosureRadiusM = 150.0;
constexpr double kMinSlowFactor = 1.1;
constexpr double kMaxSlowFactor = 20.0;
constexpr double kDefaultSlowFactor = 3.0;

// Reads body["closures"] into `out`. Returns false and sets err on bad input.
bool parseClosures(const crow::json::rvalue& body, std::vector<rl::Closure>& out,
                   std::string& err) {
    if (!body.has("closures") || body["closures"].t() != crow::json::type::List) {
        err = "closures must be a list of {lat, lon} objects";
        return false;
    }
    const auto& list = body["closures"];
    if (list.size() == 0 || list.size() > kMaxClosures) {
        err = "provide between 1 and " + std::to_string(kMaxClosures) + " closures";
        return false;
    }

    auto number = [](const crow::json::rvalue& item, const char* key, double fallback,
                     bool required, double& value, std::string& why) {
        if (!item.has(key)) {
            value = fallback;
            if (required) {
                why = std::string("closure is missing ") + key;
            }
            return !required;
        }
        if (item[key].t() != crow::json::type::Number || !std::isfinite(item[key].d())) {
            why = std::string("closure ") + key + " must be a number";
            return false;
        }
        value = item[key].d();
        return true;
    };

    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& item = list[i];
        if (item.t() != crow::json::type::Object) {
            err = "each closure must be an object";
            return false;
        }
        rl::Closure c;
        double lat = 0, lon = 0, radius = 0, factor = 0;
        if (!number(item, "lat", 0, true, lat, err) || !number(item, "lon", 0, true, lon, err) ||
            !number(item, "radius_m", kDefaultClosureRadiusM, false, radius, err) ||
            !number(item, "slow_factor", kDefaultSlowFactor, false, factor, err)) {
            return false;
        }
        if (lat < rl::kMinLat || lat > rl::kMaxLat || lon < rl::kMinLon || lon > rl::kMaxLon) {
            err = "closure is outside the Dehradun road network";
            return false;
        }
        if (radius < kMinClosureRadiusM || radius > kMaxClosureRadiusM) {
            err = "closure radius_m must be between 20 and 2000";
            return false;
        }
        c.block = true;
        if (item.has("mode")) {
            if (item["mode"].t() != crow::json::type::String) {
                err = "closure mode must be \"block\" or \"slow\"";
                return false;
            }
            const std::string mode = item["mode"].s();
            if (mode == "slow") {
                c.block = false;
            } else if (mode != "block") {
                err = "closure mode must be \"block\" or \"slow\"";
                return false;
            }
        }
        if (!c.block && (factor < kMinSlowFactor || factor > kMaxSlowFactor)) {
            err = "closure slow_factor must be between 1.1 and 20";
            return false;
        }
        c.center = {lat, lon};
        c.radiusM = radius;
        c.slowFactor = factor;
        out.push_back(c);
    }
    return true;
}

}  // namespace

int main() {
    crow::SimpleApp app;

    // Shared state. LRUCache is not thread-safe and Crow is multithreaded,
    // so every cache access below is guarded by cacheMutex.
    rl::RateLimiter trafficLimiter(5, std::chrono::seconds(1));
    rl::LRUCache<std::string, double> trafficCache(1024);
    std::mutex cacheMutex;


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

    // Connect routing only after the OSM network has loaded.
   std::unique_ptr<rl::RouteProvider> provider;
   if (snapper && network.graph) {
       provider = std::make_unique<rl::OsmRouteProvider>(network);
       CROW_LOG_INFO << "OSM routing provider: "
                     << (provider->available() ? "ready" : "unavailable");
   } else {
       provider = std::make_unique<rl::NullRouteProvider>();
   }

   CROW_ROUTE(app, "/api/health")
    ([&] {
        crow::json::wvalue response;
        response["status"] = "ok";
        response["service"] = "RouteLens API";
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
        response["rate_limit"]["max_requests"] = 5;
        response["rate_limit"]["window_seconds"] = 1;
        response["rate_limit"]["current_count"] = trafficLimiter.current_count();
        {
            std::lock_guard<std::mutex> lock(tomtomMutex);
            response["tomtom"]["requests_today"] = tomtom.requestsToday();
            response["tomtom"]["requests_this_run"] = tomtom.requestsThisRun();
            response["tomtom"]["cache_hits"] = tomtom.cacheHits();
            response["tomtom"]["daily_cap"] = ttOptions.dailyCap;
        }
        response["tomtom"]["self_test"] = selfTestStatus;
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

        crow::json::wvalue out;
        out["scenario"] = parsed->scenario;
        addSnapped(out);
        putRoutes(out, provider->compute(*parsed));
        return jsonResponse(200, out);
    });

    // What-if mode: close or slow roads around chosen points, check with BFS
    // that the destination is still reachable, and compare the best route with
    // and without the closures.
    //
    // POST /api/scenario  (JSON body)
    //   from_lat, from_lon, to_lat, to_lon           required
    //   w_time, w_distance, w_junctions, w_traffic   optional priorities
    //   closures: [{ lat, lon, radius_m?, mode?, slow_factor? }]  1..20 entries
    //     radius_m    20..2000 m, default 150
    //     mode        "block" (default) or "slow"
    //     slow_factor 1.1..20, default 3 (travel-time multiplier, slow mode only)
    CROW_ROUTE(app, "/api/scenario").methods(crow::HTTPMethod::Post)
    ([&](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || body.t() != crow::json::type::Object) {
            return jsonError(400, "request body must be a JSON object");
        }

        rl::ParamGetter get = [&](const std::string& key) -> std::optional<std::string> {
            if (!body.has(key)) {
                return std::nullopt;
            }
            const auto& v = body[key];
            if (v.t() == crow::json::type::Number) {
                std::ostringstream ss;
                ss.precision(17);
                ss << v.d();
                return ss.str();
            }
            // Anything else fails the numeric validation with a clear message.
            return std::string("not-a-number");
        };
        std::string err;
        auto parsed = rl::parseRouteRequest(get, err);
        if (!parsed) {
            return jsonError(400, err);
        }

        std::vector<rl::Closure> closures;
        if (!parseClosures(body, closures, err)) {
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
        if (!provider->available()) {
            return jsonError(501, "routing engine not connected yet");
        }
        parsed->fromNode = from.node;
        parsed->toNode = to.node;

        const rl::ScenarioEffect effect =
            rl::applyClosures(network.graph, network.pos, &network.geometry, closures);
        const rl::ReachResult before = rl::bfsReachable(network.graph, from.node, to.node);
        const rl::ReachResult after =
            rl::bfsReachable(network.graph, from.node, to.node, &effect.blocked);

        crow::json::wvalue out;
        out["scenario"] = "custom";
        out["affected"]["blocked_edges"] = effect.blockedEdges;
        out["affected"]["slowed_edges"] = effect.slowedEdges;
        for (std::size_t i = 0; i < closures.size(); ++i) {
            out["closures"][i]["lat"] = closures[i].center.lat;
            out["closures"][i]["lon"] = closures[i].center.lon;
            out["closures"][i]["radius_m"] = closures[i].radiusM;
            out["closures"][i]["mode"] = closures[i].block ? "block" : "slow";
            if (!closures[i].block) {
                out["closures"][i]["slow_factor"] = closures[i].slowFactor;
            }
            out["closures"][i]["edges_affected"] = effect.edgesPerClosure[i];
        }
        out["reachable"]["baseline"] = before.reachable;
        out["reachable"]["with_closures"] = after.reachable;
        out["reachable"]["hops_baseline"] = before.hops;
        out["reachable"]["hops_with_closures"] = after.hops;
        out["routes"] = crow::json::wvalue::list();

        const std::vector<rl::RouteInfo> baseline = provider->compute(*parsed);
        if (!baseline.empty()) {
            out["baseline"]["eta_s"] = baseline.front().etaSeconds;
            out["baseline"]["distance_km"] = baseline.front().distanceKm;
        }

        if (!after.reachable) {
            out["message"] = "the closures cut off every road to the destination";
            return jsonResponse(200, out);
        }

        parsed->blockedEdges = effect.blocked;
        parsed->durationFactor = effect.durationFactor;
        const std::vector<rl::RouteInfo> routes = provider->compute(*parsed);
        putRoutes(out, routes);

        if (!baseline.empty() && !routes.empty()) {
            // The recommended route can change for reasons unrelated to the
            // closures (the user's priorities pick it), so also report how the
            // fastest route shown changed: closures can only make that slower.
            auto fastest = [](const std::vector<rl::RouteInfo>& list) {
                double best = list.front().etaSeconds;
                for (const auto& r : list) {
                    best = std::min(best, r.etaSeconds);
                }
                return best;
            };
            out["impact"]["recommended_eta_delta_s"] =
                routes.front().etaSeconds - baseline.front().etaSeconds;
            out["impact"]["recommended_distance_delta_km"] =
                routes.front().distanceKm - baseline.front().distanceKm;
            out["impact"]["fastest_eta_delta_s"] = fastest(routes) - fastest(baseline);
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
