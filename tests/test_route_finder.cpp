// test_route_finder.cpp — the RouteFinder hierarchy: DijkstraRouter and
// PenaltyRouter on a hand-made graph, called directly and through a base pointer.
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/graph.h"
#include "../src/oop/route_finder.hpp"

using namespace rl;

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #c "\n"; } } while (0)

template <class F>
static bool throwsInvalid(F f) {
    try { f(); } catch (const std::invalid_argument&) { return true; }
    return false;
}

static bool near(double a, double b) { return std::fabs(a - b) <= 1e-9; }

int main() {
    // Three ways from 0 to 3, and node 4 that nothing reaches:
    //   e0: 0->1 (1)  e1: 1->3 (1)      route A, cost 2
    //   e2: 0->2 (2)  e3: 2->3 (2)      route B, cost 4
    //   e4: 0->3 (10)                   route C, cost 10
    Graph* g = graph_create(5);
    graph_add_edge(g, 0, 1, 1, 1, 0, nullptr, 0);
    graph_add_edge(g, 1, 3, 1, 1, 0, nullptr, 0);
    graph_add_edge(g, 0, 2, 1, 1, 0, nullptr, 0);
    graph_add_edge(g, 2, 3, 1, 1, 0, nullptr, 0);
    graph_add_edge(g, 0, 3, 1, 1, 0, nullptr, 0);
    const std::vector<double> cost{ 1, 1, 2, 2, 10 };
    const Route routeA({ 0, 1 }, 2.0), routeB({ 2, 3 }, 4.0);

    // ---- DijkstraRouter: the cheapest route only
    DijkstraRouter dijkstra(g, cost);
    std::vector<Route> d = dijkstra.find(0, 3);
    CHECK(d.size() == 1);
    CHECK(d.size() == 1 && d[0] == routeA && near(d[0].cost(), 2.0));
    CHECK(dijkstra.find(0, 4).empty());                 // unreachable: no route, no error
    CHECK(dijkstra.find(3, 3).size() == 1 && dijkstra.find(3, 3)[0].empty());
    CHECK(std::string(dijkstra.name()) == "dijkstra");

    // closures: block e1, so the cheapest is route B
    std::vector<char> blocked(g->edge_count, 0);
    blocked[1] = 1;
    DijkstraRouter closed(g, cost, blocked);
    std::vector<Route> c = closed.find(0, 3);
    CHECK(c.size() == 1 && c[0] == routeB);

    // the router keeps its own copy of the costs
    std::vector<double> changing = cost;
    DijkstraRouter copy(g, changing);
    changing[0] = 100.0;
    CHECK(copy.find(0, 3)[0] == routeA);

    // ---- PenaltyRouter: cheapest first, then alternatives, real costs
    PenaltyRouter penalty(g, cost);
    std::vector<Route> p = penalty.find(0, 3);
    CHECK(p.size() >= 2 && p.size() <= 3);
    CHECK(!p.empty() && p[0] == d[0]);                  // same first route as DijkstraRouter
    CHECK(p.size() >= 2 && p[1] == routeB && near(p[1].cost(), 4.0));   // unpenalised cost
    for (std::size_t i = 0; i < p.size(); ++i)
        for (std::size_t j = 0; j < i; ++j) CHECK(p[i] != p[j]);
    CHECK(penalty.find(0, 4).empty());
    CHECK(std::string(penalty.name()) == "penalty");

    ::PenaltyOptions one = penalty_default_options();
    one.k = 1;
    CHECK(PenaltyRouter(g, cost, {}, one).find(0, 3).size() == 1);

    // ---- polymorphism: the same call through a base pointer runs the right search
    std::vector<std::unique_ptr<RouteFinder>> finders;
    finders.push_back(std::make_unique<DijkstraRouter>(g, cost));
    finders.push_back(std::make_unique<PenaltyRouter>(g, cost));
    CHECK(finders[0]->find(0, 3).size() == 1);
    CHECK(finders[1]->find(0, 3).size() >= 2);
    const DijkstraRouter& asParent = penalty;           // inheritance: a PenaltyRouter IS-A DijkstraRouter
    CHECK(asParent.find(0, 3).size() >= 2);             // virtual: still the penalty search

    // ---- invalid input is refused when the object is made, or when asked
    CHECK(throwsInvalid([&] { DijkstraRouter(nullptr, cost); }));
    CHECK(throwsInvalid([&] { DijkstraRouter(g, std::vector<double>{ 1, 2 }); }));
    CHECK(throwsInvalid([&] { DijkstraRouter(g, std::vector<double>{ 1, 1, -2, 2, 10 }); }));
    CHECK(throwsInvalid([&] { DijkstraRouter(g, std::vector<double>{ 1, 1, NAN, 2, 10 }); }));
    CHECK(throwsInvalid([&] { DijkstraRouter(g, cost, std::vector<char>(2, 0)); }));
    ::PenaltyOptions bad = penalty_default_options();
    bad.factor = 1.0;
    CHECK(throwsInvalid([&] { PenaltyRouter(g, cost, {}, bad); }));
    bad = penalty_default_options();
    bad.max_overlap = 0.0;
    CHECK(throwsInvalid([&] { PenaltyRouter(g, cost, {}, bad); }));
    CHECK(throwsInvalid([&] { dijkstra.find(-1, 3); }));
    CHECK(throwsInvalid([&] { penalty.find(0, 5); }));

    graph_free(g);
    std::cout << "test_route_finder: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
