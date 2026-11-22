// test_c_routing_real.cpp — the C routing core and its C++ router classes on the REAL
// Dehradun network (data/osm). Skipped, and reported as skipped, if data/osm is absent.
//
//   * 1,000 random pairs, time costs, and 200 with distance costs: every route the C
//     Dijkstra returns is a valid chain of edges from source to destination whose edge
//     costs add up to the reported cost, and DijkstraRouter returns the same route;
//   * penalty method on 100 random pairs: the first route is the cheapest, every route is
//     valid, routes are different, reported costs are the real ones, and PenaltyRouter
//     returns the same routes.
//
// Whether the costs are the SHORTEST is checked against NetworkX by
// evaluation/run_evaluation.sh (docs/evaluation_phase3.md); this test needs no Python.
// (Until Phase III it compared against the C++ Dijkstra, now removed from the build.)
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

#include "../src/core/dijkstra.h"
#include "../src/core/penalty.h"
#include "../src/data/osm_loader.hpp"
#include "../src/oop/route.hpp"
#include "../src/oop/route_finder.hpp"

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

static void checkDijkstra(const Graph* g, const Weights& w, const char* name, int pairs, unsigned seed) {
    std::vector<double> cost(g->edge_count);
    CHECK(graph_compute_costs(g, &w, cost.data()) == 1);
    const rl::DijkstraRouter router(g, cost);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pick(0, g->node_count - 1);

    double ms = 0;
    int found = 0, routerAgrees = 0;
    for (int i = 0; i < pairs; i++) {
        int src = pick(rng), dst = pick(rng);
        PathResult c;
        auto t0 = Clock::now();
        CHECK(dijkstra_path(g, cost.data(), src, dst, nullptr, &c) == 0);
        ms += msSince(t0);
        const std::vector<rl::Route> viaRouter = router.find(src, dst);
        if (c.found) {
            found++;
            CHECK(validRoute(g, c, src, dst, cost));
            if (viaRouter.size() == 1 && viaRouter[0] == rl::Route::fromPath(c)) routerAgrees++;
        } else if (viaRouter.empty()) {
            routerAgrees++;
        }
        path_free(&c);
    }
    CHECK(routerAgrees == pairs);
    std::cout << name << ": " << pairs << " pairs, " << found << " reachable, all valid; DijkstraRouter agrees on "
              << routerAgrees << "; C " << ms << " ms in total\n";
}

static void checkPenalty(const Graph* g, int pairs) {
    Weights w{ 0.0, 1.0, 0.0, 0.0 };
    std::vector<double> cost(g->edge_count);
    graph_compute_costs(g, &w, cost.data());
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> pick(0, g->node_count - 1);
    int totalRoutes = 0, withAlternatives = 0;
    PenaltyOptions opt = penalty_default_options();
    const rl::PenaltyRouter router(g, cost, {}, opt);
    for (int i = 0; i < pairs; i++) {
        int src = pick(rng), dst = pick(rng);
        PathResult best;
        CHECK(dijkstra_path(g, cost.data(), src, dst, nullptr, &best) == 0);
        PenaltyResult res;
        CHECK(penalty_routes(g, cost.data(), src, dst, nullptr, &opt, &res) == 0);
        const std::vector<rl::Route> viaRouter = router.find(src, dst);
        CHECK(static_cast<int>(viaRouter.size()) == res.count);
        if (!best.found) {
            CHECK(res.count == 0);
        } else {
            CHECK(res.count >= 1 && res.count <= opt.k);
            CHECK(std::fabs(res.routes[0].cost - best.cost) <= 1e-9 * (1.0 + best.cost));
            for (int r = 0; r < res.count; r++) {
                CHECK(validRoute(g, res.routes[r], src, dst, cost));
                for (int j = 0; j < r; j++) CHECK(!sameRoute(res.routes[r], res.routes[j]) || res.routes[r].edges.size == 0);
                if (r < static_cast<int>(viaRouter.size())) CHECK(viaRouter[r] == rl::Route::fromPath(res.routes[r]));
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

    checkDijkstra(net.graph, Weights{ 0.0, 1.0, 0.0, 0.0 }, "time costs", 1000, 42);
    checkDijkstra(net.graph, Weights{ 1.0, 0.0, 0.0, 0.0 }, "distance costs", 200, 43);
    checkPenalty(net.graph, 100);

    std::cout << "test_c_routing_real: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
