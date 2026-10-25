
#ifndef RL_ASTAR_HPP
#define RL_ASTAR_HPP

#include "../core/graph.h"
#include "../data/geo.hpp"

#include <cstddef>
#include <vector>

namespace rl {

struct AStarResult {
    bool found = false;
    double total_cost = 0.0;
    std::vector<int> path;
    // Vertices settled before the search stopped (for algorithm evaluation).
    std::size_t nodes_expanded = 0;
};

// Upper bound on travel speed (metres per second) over all edges, used to turn
// straight-line distance into a lower bound on travel time. Returns +infinity
// when some edge has zero duration but non-zero length (no usable time bound)
// and 0 for a graph without edges. O(E); compute once and reuse.
double graphMaxSpeedMps(const Graph* graph);

// A* with an admissible and consistent heuristic:
//   h(v) = per_km * km(v, goal) + per_minute * minutes(v, goal)
// where km is the straight-line (haversine) distance and minutes uses the
// fastest edge speed in the graph. The congestion and signal terms of the edge
// cost are non-negative, so dropping them keeps h a lower bound. Edge lengths
// must be at least the straight-line distance between their endpoints (true
// for OSM road lengths), and positions[v] must be the coordinates of vertex v.
AStarResult astar(
    const Graph* graph,
    const Weights* weights,
    const std::vector<LatLon>* positions,
    int source,
    int destination
);

// Same search over caller-supplied per-edge costs. Every cost must be at least
// the cost graph_compute_costs() gives for `weights`, otherwise the heuristic
// can overestimate. maxSpeedMps <= 0 means "compute graphMaxSpeedMps()".
AStarResult astarWithCosts(
    const Graph* graph,
    const std::vector<double>& edgeCosts,
    const Weights* weights,
    const std::vector<LatLon>* positions,
    int source,
    int destination,
    double maxSpeedMps = 0.0
);

} // namespace rl

#endif
