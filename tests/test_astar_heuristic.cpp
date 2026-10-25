
// Property test: A* with the straight-line heuristic must return exactly the
// same optimal cost as Dijkstra for every weight mix, and must not expand
// more vertices overall.

#include "../src/algorithms/astar.hpp"
#include "../src/algorithms/dijkstra.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
#include <utility>
#include <vector>

int main() {
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    // Random geometric graph around Dehradun: nodes in a 0.05 x 0.05 degree
    // box, each linked to its few nearest neighbours in both directions.
    // Road length is 1.0-1.6x the straight line, speed 3-20 m/s.
    const int n = 400;
    std::vector<rl::LatLon> pos(n);
    for (auto& p : pos) {
        p.lat = 30.30 + 0.05 * unit(rng);
        p.lon = 78.00 + 0.05 * unit(rng);
    }

    Graph* g = graph_create(n);
    assert(g != nullptr);

    for (int u = 0; u < n; ++u) {
        // Link to the 4 nearest other nodes.
        std::vector<std::pair<double, int>> near;
        for (int v = 0; v < n; ++v) {
            if (v != u) near.push_back({rl::haversineMeters(pos[u], pos[v]), v});
        }
        std::partial_sort(near.begin(), near.begin() + 4, near.end());

        for (int i = 0; i < 4; ++i) {
            const int v = near[i].second;
            const double length = near[i].first * (1.0 + 0.6 * unit(rng));
            const double speed = 3.0 + 17.0 * unit(rng);
            const long long signalId = 1;
            const int signals = unit(rng) < 0.1 ? 1 : 0;
            assert(graph_add_edge(g, u, v, length, length / speed,
                                  0.5 * unit(rng), &signalId, signals) >= 0);
            assert(graph_add_edge(g, v, u, length, length / speed,
                                  0.5 * unit(rng), &signalId, signals) >= 0);
        }
    }

    const Weights mixes[] = {
        {1.0, 0.0, 0.0, 0.0},   // distance only
        {0.0, 1.0, 0.0, 0.0},   // time only
        {0.0, 0.0, 1.0, 0.0},   // congestion only (heuristic is then zero)
        {0.3, 0.3, 0.2, 0.2},   // balanced
        {2.0, 0.5, 1.0, 5.0},   // odd mix with signals
    };

    int compared = 0;
    for (const Weights& w : mixes) {
        std::size_t dijkstraExpanded = 0;
        std::size_t astarExpanded = 0;

        for (int trial = 0; trial < 150; ++trial) {
            const int s = static_cast<int>(unit(rng) * n) % n;
            const int t = static_cast<int>(unit(rng) * n) % n;

            rl::DijkstraResult d = rl::dijkstra(g, &w, s, t);
            rl::AStarResult a = rl::astar(g, &w, &pos, s, t);

            assert(d.found == a.found);
            if (!d.found) continue;

            // Same optimal cost, not just a similar one.
            assert(std::abs(d.total_cost - a.total_cost) <=
                   1e-9 * (1.0 + d.total_cost));

            // The returned path really has that cost.
            assert(a.path.front() == s && a.path.back() == t);

            dijkstraExpanded += d.nodes_expanded;
            astarExpanded += a.nodes_expanded;
            ++compared;
        }

        assert(astarExpanded <= dijkstraExpanded);
    }

    assert(compared > 300);

    // Distance weights make the heuristic informative: A* must beat Dijkstra.
    {
        const Weights w = {1.0, 0.0, 0.0, 0.0};
        std::size_t dijkstraExpanded = 0;
        std::size_t astarExpanded = 0;
        for (int trial = 0; trial < 100; ++trial) {
            const int s = trial % n;
            const int t = (trial * 7 + 101) % n;
            rl::DijkstraResult d = rl::dijkstra(g, &w, s, t);
            rl::AStarResult a = rl::astar(g, &w, &pos, s, t);
            if (!d.found || !a.found) continue;
            dijkstraExpanded += d.nodes_expanded;
            astarExpanded += a.nodes_expanded;
        }
        assert(astarExpanded < dijkstraExpanded);
    }

    // Zero-duration edges disable the time bound instead of breaking it.
    {
        Graph* z = graph_create(3);
        assert(z != nullptr);
        graph_add_edge(z, 0, 1, 1000, 0, 0, nullptr, 0);
        graph_add_edge(z, 1, 2, 1000, 0, 0, nullptr, 0);
        assert(std::isinf(rl::graphMaxSpeedMps(z)));
        std::vector<rl::LatLon> p = {{30.0, 78.0}, {30.001, 78.0}, {30.002, 78.0}};
        const Weights time = {0.0, 1.0, 0.0, 0.0};
        rl::AStarResult r = rl::astar(z, &time, &p, 0, 2);
        assert(r.found && std::abs(r.total_cost) < 1e-12);
        graph_free(z);
    }

    assert(rl::graphMaxSpeedMps(nullptr) == 0.0);

    graph_free(g);

    std::cout << "All A* heuristic tests passed.\n";
    return 0;
}
