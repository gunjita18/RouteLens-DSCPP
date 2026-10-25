
#ifndef RL_SCENARIO_HPP
#define RL_SCENARIO_HPP

#include "../core/graph.h"
#include "../data/geo.hpp"

#include <vector>

namespace rl {

// A what-if road closure or slowdown: every road passing within radiusM of
// center is affected.
struct Closure {
    LatLon center;
    double radiusM = 100.0;
    bool block = true;         // true: road unusable; false: road slowed
    double slowFactor = 3.0;   // slowdown only: travel time multiplier (> 1)
};

// Per-edge outcome of applying a set of closures to a graph.
struct ScenarioEffect {
    std::vector<bool> blocked;           // edge_count entries
    std::vector<double> durationFactor;  // edge_count entries, 1 = unchanged
    int blockedEdges = 0;
    int slowedEdges = 0;
    // Edges touched by each closure (parallel to the closures argument).
    std::vector<int> edgesPerClosure;
};

// Marks the edges affected by each closure. An edge is affected when its road
// shape (geometry[edge], or the straight segment between its end nodes when
// the shape is missing) passes within the closure radius. A blocked edge wins
// over a slowdown; overlapping slowdowns multiply.
// positions: vertex coordinates (size == node_count). geometry may be null.
ScenarioEffect applyClosures(
    const Graph* graph,
    const std::vector<LatLon>& positions,
    const std::vector<std::vector<LatLon>>* geometry,
    const std::vector<Closure>& closures
);

// Edge costs under a scenario: like graph_compute_costs(), but travel-time
// terms (per_minute and per_congested_minute) are scaled by the edge's
// durationFactor. Returns an empty vector for invalid input. Blocked edges keep
// their normal cost here; pass effect.blocked to the search to exclude them.
std::vector<double> scenarioCosts(
    const Graph* graph,
    const Weights* weights,
    const ScenarioEffect& effect
);

struct ReachResult {
    bool reachable = false;
    int hops = -1;      // fewest edges from source to destination, -1 if none
    int visited = 0;    // vertices reached by the search
};

// Breadth-first search ignoring edge costs: can destination still be reached
// from source, and in how few road segments? blockedEdges is optional.
ReachResult bfsReachable(
    const Graph* graph,
    int source,
    int destination,
    const std::vector<bool>* blockedEdges = nullptr
);

} // namespace rl

#endif
