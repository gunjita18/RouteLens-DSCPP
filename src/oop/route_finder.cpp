// route_finder.cpp — see route_finder.hpp.
#include "route_finder.hpp"

#include <cmath>
#include <new>
#include <stdexcept>
#include <utility>

#include "../core/dijkstra.h"

namespace rl {

namespace {

// Same rules as options_valid() in penalty.c, checked here so a bad option is a
// clear exception at construction instead of an empty result later.
bool penaltyOptionsValid(const ::PenaltyOptions& o) {
    if (o.k < 1 || o.k > PENALTY_MAX_K) return false;
    if (!std::isfinite(o.factor) || o.factor <= 1.0) return false;
    if (!(o.max_overlap > 0.0 && o.max_overlap <= 1.0)) return false;
    if (o.max_stretch != 0.0 && !(std::isfinite(o.max_stretch) && o.max_stretch >= 1.0)) {
        return false;
    }
    return o.max_iter >= 0;
}

// Owns a PenaltyResult and frees it on every path out of PenaltyRouter::find,
// including an exception (RAII).
struct PenaltyResultGuard {
    ::PenaltyResult result{};
    ~PenaltyResultGuard() { penalty_free(&result); }
};

struct PathResultGuard {
    ::PathResult result;
    PathResultGuard() { path_init(&result); }
    ~PathResultGuard() { path_free(&result); }
};

}  // namespace

// ---- RouteFinder ---------------------------------------------------------------

RouteFinder::RouteFinder(const Graph* graph, std::vector<double> costs,
                         std::vector<char> blocked)
    : graph_(graph), costs_(std::move(costs)), blocked_(std::move(blocked)) {
    if (graph_ == nullptr) throw std::invalid_argument("RouteFinder: no graph");
    const auto edges = static_cast<std::size_t>(graph_->edge_count);
    if (costs_.size() != edges) {
        throw std::invalid_argument("RouteFinder: need one cost per edge");
    }
    if (!blocked_.empty() && blocked_.size() != edges) {
        throw std::invalid_argument("RouteFinder: closures need one flag per edge");
    }
    for (double c : costs_) {
        if (!std::isfinite(c) || c < 0.0) {
            throw std::invalid_argument("RouteFinder: costs must be finite and not negative");
        }
    }
}

void RouteFinder::checkNodes(int src, int dst) const {
    const int n = graph_->node_count;
    if (src < 0 || src >= n || dst < 0 || dst >= n) {
        throw std::invalid_argument("RouteFinder: node outside the graph");
    }
}

// ---- DijkstraRouter ------------------------------------------------------------

DijkstraRouter::DijkstraRouter(const Graph* graph, std::vector<double> costs,
                               std::vector<char> blocked)
    : RouteFinder(graph, std::move(costs), std::move(blocked)) {}

bool DijkstraRouter::cheapest(int src, int dst, Route& out) const {
    checkNodes(src, dst);
    PathResultGuard path;
    // Graph, costs and nodes were all checked, so -1 can only mean out of memory.
    if (dijkstra_path(graph(), costs(), src, dst, blocked(), &path.result) != 0) {
        throw std::bad_alloc();
    }
    if (!path.result.found) return false;
    out = Route::fromPath(path.result);
    return true;
}

std::vector<Route> DijkstraRouter::find(int src, int dst) const {
    std::vector<Route> routes;
    Route best;
    if (cheapest(src, dst, best)) routes.push_back(std::move(best));
    return routes;
}

// ---- PenaltyRouter -------------------------------------------------------------

PenaltyRouter::PenaltyRouter(const Graph* graph, std::vector<double> costs,
                             std::vector<char> blocked, const ::PenaltyOptions& options)
    : DijkstraRouter(graph, std::move(costs), std::move(blocked)), options_(options) {
    if (!penaltyOptionsValid(options_)) {
        throw std::invalid_argument("PenaltyRouter: invalid penalty options");
    }
}

std::vector<Route> PenaltyRouter::find(int src, int dst) const {
    checkNodes(src, dst);
    PenaltyResultGuard res;
    if (penalty_routes(graph(), costs(), src, dst, blocked(), &options_, &res.result) != 0) {
        throw std::bad_alloc();   // everything else was checked: out of memory
    }
    std::vector<Route> routes;
    routes.reserve(static_cast<std::size_t>(res.result.count));
    for (int i = 0; i < res.result.count; ++i) {
        routes.push_back(Route::fromPath(res.result.routes[i]));
    }
    return routes;
}

}  // namespace rl
