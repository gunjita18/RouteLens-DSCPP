
#ifndef RL_ROUTE_RANKING_HPP
#define RL_ROUTE_RANKING_HPP

#include <vector>

namespace rl {

struct RouteMetrics {
    double distance_m = 0.0;
    double duration_s = 0.0;
    double traffic_index = 0.0;
    std::vector<int> path;
};

struct RoutePreferences {
    double distance_weight = 1.0;
    double duration_weight = 1.0;
    double traffic_weight = 1.0;
};

struct RankedRoutes {
    std::vector<RouteMetrics> pareto_routes;
    std::vector<RouteMetrics> ranked_routes;
};

RankedRoutes rankRoutes(
    const std::vector<RouteMetrics>& routes,
    const RoutePreferences& preferences
);

} // namespace rl

#endif
