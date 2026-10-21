#ifndef RL_DIJKSTRA_HPP
#define RL_DIJKSTRA_HPP

#include "../core/graph.h"
#include <vector>

namespace rl {

struct DijkstraResult {
    bool found = false;
    double total_cost = 0.0;
    std::vector<int> path;
};

// Finds the minimum-cost path using the supplied non-negative edge weights.
DijkstraResult dijkstra(const Graph* graph,
                        const Weights* weights,
                        int source,
                        int destination);

} // namespace rl

#endif

