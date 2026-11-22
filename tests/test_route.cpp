// test_route.cpp — the Route class: copying a C PathResult, vertices, length,
// operator< (cheaper first), operator== and operator<<.
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "../src/core/dijkstra.h"
#include "../src/core/graph.h"
#include "../src/oop/route.hpp"

using namespace rl;

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #c "\n"; } } while (0)

template <class F>
static bool throwsInvalid(F f) {
    try { f(); } catch (const std::invalid_argument&) { return true; }
    return false;
}

int main() {
    // 0 -> 1 -> 2, plus a direct 0 -> 2.
    Graph* g = graph_create(3);
    CHECK(graph_add_edge(g, 0, 1, 100.0, 10.0, 0.0, nullptr, 0) == 0);
    CHECK(graph_add_edge(g, 1, 2, 150.0, 15.0, 0.0, nullptr, 0) == 1);
    CHECK(graph_add_edge(g, 0, 2, 400.0, 40.0, 0.0, nullptr, 0) == 2);
    const std::vector<double> cost{ 10.0, 15.0, 40.0 };

    // fromPath copies the C result; the Route stays valid after path_free.
    PathResult p;
    CHECK(dijkstra_path(g, cost.data(), 0, 2, nullptr, &p) == 0);
    CHECK(p.found == 1);
    Route r = Route::fromPath(p);
    path_free(&p);
    CHECK(r.edgeCount() == 2);
    CHECK(r.edges() == std::vector<int>({ 0, 1 }));
    CHECK(std::fabs(r.cost() - 25.0) < 1e-12);
    CHECK(!r.empty());

    // vertices and length
    CHECK(r.vertices(g, 0) == std::vector<int>({ 0, 1, 2 }));
    CHECK(std::fabs(r.distanceMeters(g) - 250.0) < 1e-12);

    // "not found" cannot become a Route
    PathResult none;
    path_init(&none);
    CHECK(throwsInvalid([&] { Route::fromPath(none); }));
    path_free(&none);

    // a bad edge index is reported, not read out of bounds
    Route bad({ 7 }, 1.0);
    CHECK(throwsInvalid([&] { bad.vertices(g, 0); }));
    CHECK(throwsInvalid([&] { bad.distanceMeters(g); }));

    // operator< orders by cost, so sorting puts the cheapest first
    Route direct({ 2 }, 40.0);
    Route viaOne({ 0, 1 }, 25.0);
    std::vector<Route> routes{ direct, viaOne };
    std::sort(routes.begin(), routes.end());
    CHECK(routes.front() == viaOne);
    CHECK(viaOne < direct && !(direct < viaOne));

    // operator== compares the edges
    CHECK(Route({ 0, 1 }, 25.0) == viaOne);
    CHECK(direct != viaOne);

    // empty route: source equal to destination
    PathResult same;
    CHECK(dijkstra_path(g, cost.data(), 1, 1, nullptr, &same) == 0);
    Route stay = Route::fromPath(same);
    path_free(&same);
    CHECK(stay.empty() && stay.cost() == 0.0);
    CHECK(stay.vertices(g, 1) == std::vector<int>({ 1 }));

    // operator<<
    std::ostringstream out;
    out << viaOne;
    CHECK(out.str() == "Route(cost=25, edges=2: 0 .. 1)");
    std::ostringstream outEmpty;
    outEmpty << Route();
    CHECK(outEmpty.str() == "Route(cost=0, edges=0)");

    graph_free(g);
    std::cout << "test_route: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
