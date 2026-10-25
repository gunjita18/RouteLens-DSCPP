
#include "route_ranking.hpp"

#include <algorithm>
#include <cmath>

namespace rl {

namespace {

bool valid(const RouteMetrics& r) {
    return std::isfinite(r.distance_m) &&
           r.distance_m >= 0 &&
           std::isfinite(r.duration_s) &&
           r.duration_s >= 0 &&
           std::isfinite(r.traffic_index) &&
           r.traffic_index >= 0 &&
           r.traffic_index <= 1;
}

bool dominates(const RouteMetrics& a, const RouteMetrics& b) {
    bool noWorse =
        a.distance_m <= b.distance_m &&
        a.duration_s <= b.duration_s &&
        a.traffic_index <= b.traffic_index;

    bool strictlyBetter =
        a.distance_m < b.distance_m ||
        a.duration_s < b.duration_s ||
        a.traffic_index < b.traffic_index;

    return noWorse && strictlyBetter;
}

double score(const RouteMetrics& r, const RoutePreferences& p) {
    return p.distance_weight * r.distance_m +
           p.duration_weight * r.duration_s +
           p.traffic_weight * r.traffic_index;
}

} // namespace

RankedRoutes rankRoutes(
    const std::vector<RouteMetrics>& routes,
    const RoutePreferences& preferences
) {
    RankedRoutes result;

    if (!std::isfinite(preferences.distance_weight) ||
        !std::isfinite(preferences.duration_weight) ||
        !std::isfinite(preferences.traffic_weight) ||
        preferences.distance_weight < 0 ||
        preferences.duration_weight < 0 ||
        preferences.traffic_weight < 0) {
        return result;
    }

    std::vector<RouteMetrics> validRoutes;

    for (const auto& route : routes) {
        if (valid(route)) {
            validRoutes.push_back(route);
        }
    }

    // Pareto filtering: remove dominated routes.
    for (std::size_t i = 0; i < validRoutes.size(); ++i) {
        bool isDominated = false;

        for (std::size_t j = 0; j < validRoutes.size(); ++j) {
            if (i != j &&
                dominates(validRoutes[j], validRoutes[i])) {
                isDominated = true;
                break;
            }
        }

        if (!isDominated) {
            result.pareto_routes.push_back(validRoutes[i]);
        }
    }

    // Rank Pareto routes using user preferences.
    result.ranked_routes = result.pareto_routes;

    std::stable_sort(
        result.ranked_routes.begin(),
        result.ranked_routes.end(),
        [&](const RouteMetrics& a, const RouteMetrics& b) {
            return score(a, preferences) <
                   score(b, preferences);
        }
    );

    return result;
}

} // namespace rl
