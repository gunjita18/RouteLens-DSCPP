// route_finder.hpp — route searches as a class hierarchy over the C routing core.
//
//   RouteFinder (abstract)          find(src, dst) -> routes, cheapest first
//     DijkstraRouter                one route: the cheapest (dijkstra_path in C)
//       PenaltyRouter               the cheapest plus alternatives (penalty_routes in C)
//
// OOP concepts shown here:
//   abstraction     callers hold a RouteFinder& / std::unique_ptr<RouteFinder> and call
//                   find() without knowing which search runs
//   inheritance     PenaltyRouter IS-A DijkstraRouter: it reuses the stored graph, costs
//                   and closures, and its first route is exactly DijkstraRouter's route
//   polymorphism    find() is virtual; the call through a base pointer runs the right one
//   encapsulation   graph, costs and closures are private, checked once in the constructor
//
// Every finder COPIES the cost and closure arrays it is given, so it never points into
// a vector the caller may change or free. Costs must be finite and not negative (what
// Dijkstra needs); the constructor throws std::invalid_argument otherwise, so a finder
// that exists can always search.
#pragma once
#include <memory>
#include <vector>

#include "../core/graph.h"
#include "../core/penalty.h"
#include "route.hpp"

namespace rl {

class RouteFinder {
public:
    virtual ~RouteFinder() = default;
    RouteFinder(const RouteFinder&) = delete;             // polymorphic base: no slicing
    RouteFinder& operator=(const RouteFinder&) = delete;

    // Routes from src to dst, cheapest first; empty if dst cannot be reached.
    // Throws std::invalid_argument if src or dst is not a node of the graph.
    virtual std::vector<Route> find(int src, int dst) const = 0;

    // Short name for logs and the evaluation ("dijkstra", "penalty").
    virtual const char* name() const = 0;

    const Graph* graph() const { return graph_; }

protected:
    // costs: one per edge; blocked: empty (nothing closed) or one flag per edge.
    RouteFinder(const Graph* graph, std::vector<double> costs, std::vector<char> blocked);

    const double* costs() const { return costs_.data(); }
    const char* blocked() const { return blocked_.empty() ? nullptr : blocked_.data(); }
    void checkNodes(int src, int dst) const;

private:
    const Graph* graph_;
    std::vector<double> costs_;
    std::vector<char> blocked_;
};

class DijkstraRouter : public RouteFinder {
public:
    DijkstraRouter(const Graph* graph, std::vector<double> costs,
                   std::vector<char> blocked = {});

    // At most one route: the cheapest.
    std::vector<Route> find(int src, int dst) const override;
    const char* name() const override { return "dijkstra"; }

protected:
    // The cheapest route, or false if dst cannot be reached. Used by find() and
    // available to derived classes.
    bool cheapest(int src, int dst, Route& out) const;
};

class PenaltyRouter : public DijkstraRouter {
public:
    // options: as for penalty_routes (k routes, factor 1.4, overlap 0.7, stretch limit).
    // Throws std::invalid_argument for options penalty_routes would reject.
    PenaltyRouter(const Graph* graph, std::vector<double> costs,
                  std::vector<char> blocked = {},
                  const ::PenaltyOptions& options = penalty_default_options());

    // Up to options().k routes; the first is the cheapest (DijkstraRouter's route),
    // every cost is the real, unpenalised cost.
    std::vector<Route> find(int src, int dst) const override;
    const char* name() const override { return "penalty"; }

    const ::PenaltyOptions& options() const { return options_; }

private:
    ::PenaltyOptions options_;
};

}  // namespace rl
