
#include "../src/algorithms/route_ranking.hpp"

#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using namespace rl;

    std::vector<RouteMetrics> routes = {
        {1000, 600, 0.2, {0, 1}},
        {1500, 900, 0.5, {0, 2}},
        {800, 700, 0.1, {0, 3}},
        {2000, 1200, 0.8, {0, 4}}
    };

    RoutePreferences preferences;
    preferences.distance_weight = 1.0;
    preferences.duration_weight = 1.0;
    preferences.traffic_weight = 100.0;

    RankedRoutes result = rankRoutes(routes, preferences);

    // The last route is dominated by the first route.
    assert(result.pareto_routes.size() == 2);
    assert(result.ranked_routes.size() == 2);

    // Invalid preferences should return empty results.
    preferences.distance_weight = -1.0;
    RankedRoutes invalidResult = rankRoutes(routes, preferences);
    assert(invalidResult.pareto_routes.empty());
    assert(invalidResult.ranked_routes.empty());

    std::cout << "Route ranking tests passed!\n";
    return 0;
}
