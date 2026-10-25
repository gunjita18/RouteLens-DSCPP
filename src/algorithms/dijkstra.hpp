
#ifndef RL_DIJKSTRA_HPP
#define RL_DIJKSTRA_HPP

#include "../core/graph.h"
#include <cstddef>
#include <vector>

namespace rl {

struct DijkstraResult {
    bool found = false;
    double total_cost = 0.0;
    std::vector<int> path;
    // Edge indices along the path (path.size() - 1 entries). Distinguishes
    // parallel edges between the same two vertices.
    std::vector<int> edge_path;
    // Vertices settled before the search stopped (for algorithm evaluation).
    std::size_t nodes_expanded = 0;
};

// Finds the minimum-cost path using non-negative edge weights.
// blockedEdges is optional and marks graph edge indices to ignore.
DijkstraResult dijkstra(
    const Graph* graph,
    const Weights* weights,
    int source,
    int destination,
    const std::vector<bool>* blockedEdges = nullptr
);

// Same search over caller-supplied per-edge costs (size == graph->edge_count,
// finite and non-negative). Lets the penalty method and scenario mode change
// individual edge costs without rebuilding the graph.
DijkstraResult dijkstraWithCosts(
    const Graph* graph,
    const std::vector<double>& edgeCosts,
    int source,
    int destination,
    const std::vector<bool>* blockedEdges = nullptr
);

} // namespace rl

#endif
