// test_c_routing_real.cpp — the C routing core against the existing C++ Dijkstra on the
// REAL Dehradun network (data/osm). Skipped, and reported as skipped, if data/osm is absent.
//
//   * 1,000 random pairs, time costs: the C and the C++ Dijkstra must find the same cost
//     (equal-cost routes may differ edge by edge, so the cost is compared and the C route
//     is checked to be a valid chain of edges);
//   * 200 random pairs, distance costs: same;
//   * penalty method on 100 random pairs: the first route is the cheapest, every route is valid,
//     routes are different, reported costs are the real ones.
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

#include "../src/algorithms/dijkstra.hpp"
#include "../src/core/dijkstra.h"
#include "../src/core/penalty.h"
#include "../src/data/osm_loader.hpp"

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #c "\n"; } } while (0)

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

static bool validRoute(const Graph* g, const PathResult& r, int src, int dst, const std::vector<double>& cost) {
    int at = src;
    double sum = 0.0;
    for (int i = 0; i < r.edges.size; i++) {
        int e = ia_get(&r.edges, i);
        if (e < 0 || e >= g->edge_count || g->edges[e].from != at) return false;
        at = g->edges[e].to;
        sum += cost[e];
    }
    return at == dst && std::fabs(sum - r.cost) <= 1e-9 * (1.0 + std::fabs(r.cost));
}

static bool sameRoute(const PathResult& a, const PathResult& b) {
    if (a.edges.size != b.edges.size) return false;
    for (int i = 0; i < a.edges.size; i++)
        if (ia_get(&a.edges, i) != ia_get(&b.edges, i)) return false;
    return true;
}

static void compareDijkstra(const Graph* g, const Weights& w, const char* name, int pairs, unsigned seed) {
    std::vector<double> cost(g->edge_count);
    CHECK(graph_compute_costs(g, &w, cost.data()) == 1);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pick(0, g->node_count - 1);

    double cMs = 0, cppMs = 0;
    int agree = 0, found = 0;
    for (int i = 0; i < pairs; i++) {
        int src = pick(rng), dst = pick(rng);
        PathResult c;
        auto t0 = Clock::now();
        int status = dijkstra_path(g, cost.data(), src, dst, nullptr, &c);
        cMs += msSince(t0);
        t0 = Clock::now();
        rl::DijkstraResult cpp = rl::dijkstraWithCosts(g, cost, src, dst);
        cppMs += msSince(t0);
        CHECK(status == 0);
        bool same = c.found == (cpp.found ? 1 : 0) &&
                    (!c.found || std::fabs(c.cost - cpp.total_cost) <= 1e-9 * (1.0 + std::fabs(cpp.total_cost)));
        if (same) agree++;
        if (c.found) { found++; CHECK(validRoute(g, c, src, dst, cost)); }
        path_free(&c);
    }
    CHECK(agree == pairs);
    std::cout << name << ": " << pairs << " pairs, " << found << " reachable, C and C++ agree on " << agree
              << "; C " << cMs << " ms, C++ " << cppMs << " ms in total\n";
}

static void testPenalty(const Graph* g, int pairs) {
    Weights w{ 0.0, 1.0, 0.0, 0.0 };
    std::vector<double> cost(g->edge_count);
    graph_compute_costs(g, &w, cost.data());
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> pick(0, g->node_count - 1);
    int totalRoutes = 0, withAlternatives = 0;
    PenaltyOptions opt = penalty_default_options();
    for (int i = 0; i < pairs; i++) {
        int src = pick(rng), dst = pick(rng);
        PathResult best;
        CHECK(dijkstra_path(g, cost.data(), src, dst, nullptr, &best) == 0);
        PenaltyResult res;
        CHECK(penalty_routes(g, cost.data(), src, dst, nullptr, &opt, &res) == 0);
        if (!best.found) {
            CHECK(res.count == 0);
        } else {
            CHECK(res.count >= 1 && res.count <= opt.k);
            CHECK(std::fabs(res.routes[0].cost - best.cost) <= 1e-9 * (1.0 + best.cost));
            for (int r = 0; r < res.count; r++) {
                CHECK(validRoute(g, res.routes[r], src, dst, cost));
                for (int j = 0; j < r; j++) CHECK(!sameRoute(res.routes[r], res.routes[j]) || res.routes[r].edges.size == 0);
            }
            totalRoutes += res.count;
            if (res.count > 1) withAlternatives++;
        }
        penalty_free(&res);
        path_free(&best);
    }
    std::cout << "penalty: " << pairs << " pairs, " << totalRoutes << " routes, " << withAlternatives
              << " pairs with at least one alternative\n";
}

int main() {
    std::ifstream probe("data/osm/nodes.csv");
    if (!probe) {
        std::cout << "test_c_routing_real: data/osm not found, SKIPPED (nothing was checked)\n";
        return 0;
    }
    rl::OsmNetwork net;
    std::string err;
    if (!rl::loadOsmNetwork("data/osm", net, err)) { std::cerr << "network: " << err << "\n"; return 1; }
    std::cout << "network: " << net.graph->node_count << " nodes, " << net.graph->edge_count << " edges\n";

    compareDijkstra(net.graph, Weights{ 0.0, 1.0, 0.0, 0.0 }, "time costs", 1000, 42);
    compareDijkstra(net.graph, Weights{ 1.0, 0.0, 0.0, 0.0 }, "distance costs", 200, 43);
    testPenalty(net.graph, 100);

    std::cout << "test_c_routing_real: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
