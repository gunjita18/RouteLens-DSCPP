
#ifndef RL_YEN_HPP
#define RL_YEN_HPP

#include "../core/graph.h"
#include <vector>

namespace rl {

struct YenResult {
    std::vector<std::vector<int>> paths;
    std::vector<double> total_costs;
};

// Finds up to k shortest loopless paths between two vertices.
YenResult yenKShortestPaths(
    const Graph* graph,
    const Weights* weights,
    int source,
    int destination,
    int k
);

} // namespace rl

#endif
