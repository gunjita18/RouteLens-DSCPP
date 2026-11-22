#include <algorithm>
#include <atomic>
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
#include "preview_provider.hpp"
#include "request_params.hpp"
#include "route_provider.hpp"
#include "osm_route_provider.hpp"
#include "../algorithms/scenario.hpp"
#include "../data/osm_loader.hpp"
#include "../oop/traffic_provider.hpp"
#include "../spatial/road_snapper.hpp"
#include "../traffic/junctions.hpp"

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

// Major-junction list (override with ROUTELENS_JUNCTIONS_FILE).
std::string junctionsFile() {
    const char* f = std::getenv("ROUTELENS_JUNCTIONS_FILE");
    return f ? f : "data/junctions/major_junctions.csv";
}

// Saved traffic snapshot written by preprocess/fetch_traffic.py (override with
// ROUTELENS_TRAFFIC_FILE). If it is missing the server still works, on free-flow
// speeds, and says so in /api/traffic/status.
std::string trafficFile() {
    const char* f = std::getenv("ROUTELENS_TRAFFIC_FILE");
    return f ? f : "data/traffic/traffic.csv";
}

// A click farther than this from any road is rejected.
constexpr double kMaxSnapMeters = 500.0;

// A major junction claims every graph node within this distance (the radius
// docs/TRAFFIC.md gives for the server; 41 of 41 junctions snap with it).
constexpr double kJunctionRadiusMeters = 50.0;

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
}

// Where each click landed on the road network, shown on the website. Shared by
// /api/routes and /api/scenario.
void putSnapped(crow::json::wvalue& body, const rl::SnapResult& from, const rl::SnapResult& to) {
    auto put = [&](const char* key, const rl::SnapResult& s) {
        body["snapped"][key]["node"] = s.node;
        body["snapped"][key]["lat"] = s.nodePos.lat;
        body["snapped"][key]["lon"] = s.nodePos.lon;
        body["snapped"][key]["road_class"] = s.roadClass;
        body["snapped"][key]["road_distance_m"] = s.roadDistanceM;
    };
    put("from", from);
    put("to", to);
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

    // Phase III: the route-result cache (LRU) and the request rate limiter were
    // removed from the server (outside the syllabus; with a saved traffic
    // snapshot there is no TomTom quota to protect). Both remain in Git history.

    // Route provider. It starts as NullRouteProvider (answers 501) and is
    // replaced by the real OSM routing provider once the road network has
    // loaded (below), so 501 now only means the network could not be loaded.
    // ROUTELENS_PREVIEW=1 switches on SAMPLE routes for showing the interface; they
    // are not computed from the road network and are labelled as such everywhere.
    std::unique_ptr<rl::RouteProvider> provider = std::make_unique<rl::NullRouteProvider>();
    const char* previewEnv = std::getenv("ROUTELENS_PREVIEW");
    if (previewEnv != nullptr && std::string(previewEnv) == "1") {
        provider = std::make_unique<rl::PreviewRouteProvider>();
        CROW_LOG_WARNING << "PREVIEW MODE: /api/routes returns SAMPLE routes, not real ones";
    }

    // Road network (owned by the map-data module) and the linear-search road
    // snapper that puts clicks onto it. Without the CSVs the server still runs;
    // /api/routes then answers 503.
    rl::OsmNetwork network;
    std::unique_ptr<rl::RoadSnapper> snapper;
    {
        std::string loadErr;
        const std::string dir = dataDir();
        if (rl::loadOsmNetwork(dir, network, loadErr)) {
            snapper = std::make_unique<rl::RoadSnapper>(network);
            CROW_LOG_INFO << "road network loaded from " << dir << ": " << network.stats.keptNodes
                          << " nodes, " << network.stats.keptEdges << " edges, "
                          << snapper->segmentCount() << " road segments for snapping";
        } else {
            CROW_LOG_WARNING << "road network NOT loaded from " << dir << ": " << loadErr;
        }
    }

    // Major junctions (list owned by the traffic module), snapped onto the road
    // network once at startup and only read afterwards, so requests can share it
    // without a lock. The routing engine passes it to rl::summarizeRoute() to
    // count the junctions on each route. Without the file the server still runs.
    rl::JunctionIndex junctions;
    bool junctionsLoaded = false;
    if (snapper) {
        std::string jErr;
        const std::string file = junctionsFile();
        junctionsLoaded = rl::loadJunctions(file, network, kJunctionRadiusMeters, junctions, jErr);
        if (junctionsLoaded) {
            CROW_LOG_INFO << "major junctions loaded from " << file << ": "
                          << junctions.junctions.size() - junctions.unsnapped << " of "
                          << junctions.junctions.size() << " snapped onto the road network, "
                          << junctions.skippedNoCoords << " pending (no coordinates)";
        } else {
            CROW_LOG_WARNING << "major junctions NOT loaded from " << file << ": " << jErr;
        }
    }

    // Measured traffic: the saved snapshot (preprocess/fetch_traffic.py), read once
    // at startup. The server never calls TomTom, so it works offline. A missing or
    // unreadable file is not fatal: the provider falls back to free-flow speeds and
    // the status endpoint (and the page) say "estimated".
    std::string trafficWarning;
    std::unique_ptr<rl::TrafficProvider> traffic = std::make_unique<rl::FreeFlowProvider>();
    if (snapper) {
        traffic = rl::makeTrafficProvider(network, trafficFile(), &trafficWarning);
        if (trafficWarning.empty()) {
            CROW_LOG_INFO << "traffic " << traffic->description() << ": " << traffic->edgesWithData()
                          << " of " << network.stats.keptEdges << " edges have data";
        } else {
            CROW_LOG_WARNING << trafficWarning;
        }
    }
    const bool trafficIsSnapshot = traffic->edgesWithData() > 0;

    // Connect routing only after the OSM network has loaded. Preview mode
    // (ROUTELENS_PREVIEW=1) keeps its sample provider.
    if (!provider->isPreview() && snapper && network.graph) {
        // With the junction list, routes count major junctions and wait there;
        // with the snapshot, edges that have data use the measured speed.
        provider = std::make_unique<rl::OsmRouteProvider>(
            network, junctionsLoaded ? &junctions : nullptr, traffic.get());
        CROW_LOG_INFO << "OSM routing provider: "
                      << (provider->available() ? "ready" : "unavailable");
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
        response["junctions"]["loaded"] = junctionsLoaded;
        if (junctionsLoaded) {
            response["junctions"]["count"] = static_cast<int>(junctions.junctions.size());
            response["junctions"]["snapped"] = static_cast<int>(junctions.junctions.size()) - junctions.unsnapped;
            response["junctions"]["pending"] = junctions.skippedNoCoords;
        }
        return response;
    });

    CROW_ROUTE(app, "/api/traffic/status")
    ([&] {
        crow::json::wvalue response;
        // "snapshot": measured speeds saved at fetched_at (not live); the server
        // makes no TomTom calls. "free_flow_fallback": no usable snapshot, every
        // speed is an estimate.
        response["source"] = trafficIsSnapshot ? "snapshot" : "free_flow_fallback";
        response["description"] = traffic->description();
        response["fetched_at"] = traffic->fetchedAt();
        response["edges_with_data"] = traffic->edgesWithData();
        response["warning"] = trafficWarning;
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

        if (!provider->available()) {
            crow::json::wvalue body;
            body["error"] = "routing engine not connected yet";
            putSnapped(body, from, to);
            return jsonResponse(501, body);
        }

        const std::vector<rl::RouteInfo> routes = provider->compute(*parsed);

        crow::json::wvalue out;
        out["scenario"] = parsed->scenario;
        out["preview"] = provider->isPreview();   // true = sample routes, not real
        putSnapped(out, from, to);
        putRoutes(out, routes);
        return jsonResponse(200, out);
    });

    // What-if mode: close or slow roads around chosen points, check with BFS
    // that the destination is still reachable, and compare the best route with
    // and without the closures.
    //
    // POST /api/scenario  (JSON body)
    //   from_lat, from_lon, to_lat, to_lon           required
    //   w_time, w_distance, w_junctions, w_traffic   optional priorities
    //   scenario      optional traffic scenario (normal, rush_hour, rain, incident)
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
            // The traffic scenario is the one text field; its name is checked by
            // parseRouteRequest like the query parameter of /api/routes.
            if (key == "scenario" && v.t() == crow::json::type::String) {
                return std::string(v.s());
            }
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
        out["traffic_scenario"] = parsed->scenario;   // rain etc., combined with the closures
        putSnapped(out, from, to);   // docs/scenario_api.md promises it; the website draws it
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

        // The route without closures (the same query /api/routes answers).
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
