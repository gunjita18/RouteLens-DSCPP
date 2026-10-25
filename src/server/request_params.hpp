#ifndef ROUTELENS_REQUEST_PARAMS_HPP
#define ROUTELENS_REQUEST_PARAMS_HPP

#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rl {

// Dehradun bounding box used by the OSM export (docs/OSM_DATA.md).
constexpr double kMinLat = 30.20;
constexpr double kMaxLat = 30.45;
constexpr double kMinLon = 77.90;
constexpr double kMaxLon = 78.15;

struct RouteRequest {
    double fromLat = 0, fromLon = 0, toLat = 0, toLon = 0;
    // User priorities from the website sliders, normalised to sum to 1.
    double wTime = 0.25, wDistance = 0.25, wJunctions = 0.25, wTraffic = 0.25;
    std::string scenario = "normal";
    // Graph nodes the two clicks were snapped to by the server (-1 = not snapped).
    // The routing engine searches between these.
    int fromNode = -1, toNode = -1;
    // What-if closures from /api/scenario, per graph edge (empty = none).
    // blockedEdges: edge may not be used. durationFactor: travel-time multiplier.
    std::vector<bool> blockedEdges;
    std::vector<double> durationFactor;
};

// Returns the raw query-string value for a key, or nullopt if absent.
using ParamGetter = std::function<std::optional<std::string>(const std::string&)>;

namespace detail {

inline bool parseDouble(const std::string& s, double& out) {
    try {
        std::size_t used = 0;
        out = std::stod(s, &used);
        return used == s.size() && std::isfinite(out);
    } catch (...) {
        return false;
    }
}

inline bool required(const ParamGetter& get, const char* key, double& out,
                     std::string& err) {
    auto v = get(key);
    if (!v || !parseDouble(*v, out)) {
        err = std::string("missing or non-numeric parameter: ") + key;
        return false;
    }
    return true;
}

inline bool optionalWeight(const ParamGetter& get, const char* key, double& out,
                           std::string& err) {
    auto v = get(key);
    if (!v) {
        return true;
    }
    if (!parseDouble(*v, out) || out < 0.0) {
        err = std::string("parameter must be a non-negative number: ") + key;
        return false;
    }
    return true;
}

}  // namespace detail

// Validates a /api/routes query. On failure returns nullopt and fills `err`.
inline std::optional<RouteRequest> parseRouteRequest(const ParamGetter& get,
                                                     std::string& err) {
    RouteRequest r;
    if (!detail::required(get, "from_lat", r.fromLat, err) ||
        !detail::required(get, "from_lon", r.fromLon, err) ||
        !detail::required(get, "to_lat", r.toLat, err) ||
        !detail::required(get, "to_lon", r.toLon, err)) {
        return std::nullopt;
    }

    auto inBox = [](double lat, double lon) {
        return lat >= kMinLat && lat <= kMaxLat && lon >= kMinLon && lon <= kMaxLon;
    };
    if (!inBox(r.fromLat, r.fromLon) || !inBox(r.toLat, r.toLon)) {
        err = "coordinates are outside the Dehradun road network";
        return std::nullopt;
    }

    double t = 1, d = 1, s = 1, c = 1;
    if (!detail::optionalWeight(get, "w_time", t, err) ||
        !detail::optionalWeight(get, "w_distance", d, err) ||
        !detail::optionalWeight(get, "w_junctions", s, err) ||
        !detail::optionalWeight(get, "w_traffic", c, err)) {
        return std::nullopt;
    }
    const double sum = t + d + s + c;
    if (sum <= 0.0) {
        err = "at least one priority weight must be greater than zero";
        return std::nullopt;
    }
    r.wTime = t / sum;
    r.wDistance = d / sum;
    r.wJunctions = s / sum;
    r.wTraffic = c / sum;

    if (auto sc = get("scenario")) {
        if (*sc != "normal" && *sc != "rush_hour" && *sc != "rain" &&
            *sc != "incident") {
            err = "unknown scenario (use normal, rush_hour, rain or incident)";
            return std::nullopt;
        }
        r.scenario = *sc;
    }
    return r;
}

}  // namespace rl

#endif
