
#ifndef RL_ASTAR_HPP
#define RL_ASTAR_HPP

#include "../core/graph.h"
#include "../data/geo.hpp"

#include <vector>

namespace rl {

struct AStarResult {
    bool found = false;
    double total_cost = 0.0;
    std::vector<int> path;
};

AStarResult astar(
    const Graph* graph,
    const Weights* weights,
    const std::vector<LatLon>* positions,
    int source,
    int destination
);

} // namespace rl

#endif
