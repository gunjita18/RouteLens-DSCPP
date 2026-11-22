// route.hpp — one route found by the C routing core, as a small value class.
//
// A Route holds the edge indices of the route (in travelling order) and its
// cost. The C core returns a PathResult (a C struct with an IntArray that must
// be freed by hand); Route::fromPath copies it into a std::vector, so the route
// can be copied, compared and stored without manual memory management.
//
// OOP concepts shown here:
//   encapsulation   the edge list and cost are private; read through const getters
//   operator<       routes order by cost (cheaper first), so std containers and the
//                   C merge sort can rank them
//   operator<<      a friend that prints "Route(cost=..., edges=...)" for logs and tests
#pragma once
#include <cstddef>
#include <iosfwd>
#include <vector>

#include "../core/dijkstra.h"
#include "../core/graph.h"

namespace rl {

class Route {
public:
    Route() = default;   // an empty route (no edges, cost 0)
    Route(std::vector<int> edges, double cost);

    // Copies a route found by the C core. Throws std::invalid_argument if the
    // result says "not found" (there is no route to copy).
    static Route fromPath(const ::PathResult& path);

    const std::vector<int>& edges() const { return edges_; }
    double cost() const { return cost_; }
    std::size_t edgeCount() const { return edges_.size(); }
    bool empty() const { return edges_.empty(); }

    // The nodes the route passes, starting at src: src, to(e0), to(e1), ...
    // Throws std::invalid_argument if an edge index is outside the graph.
    std::vector<int> vertices(const Graph* graph, int src) const;

    // Total road length in metres (sum of the edges' distance_m).
    double distanceMeters(const Graph* graph) const;

    // Same edges in the same order (the cost is not compared: it follows from the edges).
    bool operator==(const Route& other) const { return edges_ == other.edges_; }
    bool operator!=(const Route& other) const { return !(*this == other); }

    friend bool operator<(const Route& a, const Route& b) { return a.cost_ < b.cost_; }
    friend std::ostream& operator<<(std::ostream& os, const Route& r);

private:
    std::vector<int> edges_;
    double cost_ = 0.0;
};

}  // namespace rl
