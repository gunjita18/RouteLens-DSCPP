
#ifndef RL_PENALTY_HPP
#define RL_PENALTY_HPP

#include "../core/graph.h"
#include <vector>

namespace rl {

struct PenaltyOptions {
    // Number of distinct routes wanted (the first is the true shortest path).
    int k = 3;
    // Each time a route is found, the cost of every edge on it is multiplied
    // by this factor (> 1), pushing the next search onto different roads.
    double penaltyFactor = 1.5;
    // Upper bound on searches; 0 means 5 * k. Needed because a search can
    // return a route that was already found.
    int maxIterations = 0;
    // Discard routes costing more than maxStretch times the shortest route
    // (measured with the unpenalised costs). 0 disables the check.
    double maxStretch = 0.0;
};

struct PenaltyResult {
    std::vector<std::vector<int>> paths;      // vertex sequences
    std::vector<std::vector<int>> edge_paths; // edge indices, one per path
    std::vector<double> total_costs;          // unpenalised cost of each path
    int iterations = 0;                       // searches actually run
};

// Penalty method for alternative routes. Repeatedly runs Dijkstra on
// penalised edge costs; after each route, the edges it used become more
// expensive, so later searches are pushed onto genuinely different roads.
// Reported costs always use the original (unpenalised) costs.
//
// baseCosts must hold one finite, non-negative cost per graph edge.
// blockedEdges (optional, size == edge_count) marks edges that may not be used.
PenaltyResult penaltyAlternatives(
    const Graph* graph,
    const std::vector<double>& baseCosts,
    int source,
    int destination,
    const PenaltyOptions& options,
    const std::vector<bool>* blockedEdges = nullptr
);

// Convenience overload: costs come from the user's priority weights.
PenaltyResult penaltyAlternatives(
    const Graph* graph,
    const Weights* weights,
    int source,
    int destination,
    const PenaltyOptions& options
);

} // namespace rl

#endif
