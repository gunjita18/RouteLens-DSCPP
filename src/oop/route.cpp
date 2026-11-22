// route.cpp — see route.hpp.
#include "route.hpp"

#include <ostream>
#include <stdexcept>
#include <utility>

namespace rl {

namespace {

void checkEdge(const Graph* graph, int e) {
    if (graph == nullptr || e < 0 || e >= graph->edge_count) {
        throw std::invalid_argument("Route: edge index outside the graph");
    }
}

}  // namespace

Route::Route(std::vector<int> edges, double cost) : edges_(std::move(edges)), cost_(cost) {}

Route Route::fromPath(const ::PathResult& path) {
    if (!path.found) throw std::invalid_argument("Route::fromPath: no route was found");
    std::vector<int> edges;
    edges.reserve(static_cast<std::size_t>(path.edges.size));
    for (int i = 0; i < path.edges.size; ++i) edges.push_back(ia_get(&path.edges, i));
    return Route(std::move(edges), path.cost);
}

std::vector<int> Route::vertices(const Graph* graph, int src) const {
    std::vector<int> nodes;
    nodes.reserve(edges_.size() + 1);
    nodes.push_back(src);
    for (int e : edges_) {
        checkEdge(graph, e);
        nodes.push_back(graph->edges[e].to);
    }
    return nodes;
}

double Route::distanceMeters(const Graph* graph) const {
    double metres = 0.0;
    for (int e : edges_) {
        checkEdge(graph, e);
        metres += graph->edges[e].distance_m;
    }
    return metres;
}

std::ostream& operator<<(std::ostream& os, const Route& r) {
    os << "Route(cost=" << r.cost_ << ", edges=" << r.edges_.size();
    if (!r.edges_.empty()) os << ": " << r.edges_.front() << " .. " << r.edges_.back();
    return os << ')';
}

}  // namespace rl
