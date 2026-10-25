
#include "../src/algorithms/penalty.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <set>
#include <vector>

namespace {

// Three disjoint routes from 0 to 4 with costs 2, 3 and 4 (1 per km).
Graph* threeRoutes() {
    Graph* g = graph_create(5);
    assert(g != nullptr);
    assert(graph_add_edge(g, 0, 1, 1000, 0, 0, nullptr, 0) == 0);
    assert(graph_add_edge(g, 1, 4, 1000, 0, 0, nullptr, 0) == 1);
    assert(graph_add_edge(g, 0, 2, 1500, 0, 0, nullptr, 0) == 2);
    assert(graph_add_edge(g, 2, 4, 1500, 0, 0, nullptr, 0) == 3);
    assert(graph_add_edge(g, 0, 3, 2000, 0, 0, nullptr, 0) == 4);
    assert(graph_add_edge(g, 3, 4, 2000, 0, 0, nullptr, 0) == 5);
    return g;
}

} // namespace

int main() {
    Weights weights = {1.0, 0.0, 0.0, 0.0};

    {
        Graph* g = threeRoutes();
        rl::PenaltyOptions options;
        options.k = 3;
        options.penaltyFactor = 3.0;

        rl::PenaltyResult r =
            rl::penaltyAlternatives(g, &weights, 0, 4, options);

        // The first route is the true shortest path; later ones differ.
        assert(r.paths.size() == 3);
        assert(r.paths[0] == std::vector<int>({0, 1, 4}));
        assert(r.paths[1] == std::vector<int>({0, 2, 4}));
        assert(r.paths[2] == std::vector<int>({0, 3, 4}));

        // Reported costs ignore the penalties.
        assert(std::abs(r.total_costs[0] - 2.0) < 1e-9);
        assert(std::abs(r.total_costs[1] - 3.0) < 1e-9);
        assert(std::abs(r.total_costs[2] - 4.0) < 1e-9);

        // Edge paths match vertex paths.
        assert(r.edge_paths[0] == std::vector<int>({0, 1}));
        assert(r.edge_paths[2] == std::vector<int>({4, 5}));
        graph_free(g);
    }

    {
        // Asking for more routes than exist stops cleanly with what exists.
        Graph* g = threeRoutes();
        rl::PenaltyOptions options;
        options.k = 10;
        options.penaltyFactor = 3.0;

        rl::PenaltyResult r =
            rl::penaltyAlternatives(g, &weights, 0, 4, options);
        std::set<std::vector<int>> distinct(r.paths.begin(), r.paths.end());
        assert(r.paths.size() == 3);
        assert(distinct.size() == r.paths.size());
        assert(r.iterations <= 50);
        graph_free(g);
    }

    {
        // Blocked edges are never used.
        Graph* g = threeRoutes();
        std::vector<double> costs(g->edge_count);
        assert(graph_compute_costs(g, &weights, costs.data()) == 1);
        std::vector<bool> blocked(g->edge_count, false);
        blocked[0] = true;  // 0 -> 1

        rl::PenaltyOptions options;
        options.k = 3;
        options.penaltyFactor = 3.0;

        rl::PenaltyResult r =
            rl::penaltyAlternatives(g, costs, 0, 4, options, &blocked);
        assert(r.paths.size() == 2);
        assert(r.paths[0] == std::vector<int>({0, 2, 4}));
        assert(r.paths[1] == std::vector<int>({0, 3, 4}));
        graph_free(g);
    }

    {
        // maxStretch drops routes that are too much longer than the shortest.
        Graph* g = threeRoutes();
        rl::PenaltyOptions options;
        options.k = 3;
        options.penaltyFactor = 3.0;
        options.maxStretch = 1.2;  // 2.0 * 1.2 = 2.4 < 3.0

        rl::PenaltyResult r =
            rl::penaltyAlternatives(g, &weights, 0, 4, options);
        assert(r.paths.size() == 1);
        assert(r.paths[0] == std::vector<int>({0, 1, 4}));
        graph_free(g);
    }

    {
        // Free (zero-cost) edges can still be penalised.
        Graph* g = threeRoutes();
        std::vector<double> costs(g->edge_count, 0.0);
        rl::PenaltyOptions options;
        options.k = 2;
        options.penaltyFactor = 2.0;

        rl::PenaltyResult r =
            rl::penaltyAlternatives(g, costs, 0, 4, options);
        assert(r.paths.size() == 2);
        assert(r.paths[0] != r.paths[1]);
        graph_free(g);
    }

    {
        // Source equals destination: the zero-edge route only.
        Graph* g = threeRoutes();
        rl::PenaltyOptions options;
        rl::PenaltyResult r =
            rl::penaltyAlternatives(g, &weights, 2, 2, options);
        assert(r.paths.size() == 1);
        assert(r.paths[0] == std::vector<int>({2}));
        assert(r.total_costs[0] == 0.0);
        graph_free(g);
    }

    {
        // Invalid input returns no routes instead of failing.
        Graph* g = threeRoutes();
        rl::PenaltyOptions options;

        options.k = 0;
        assert(rl::penaltyAlternatives(g, &weights, 0, 4, options)
                   .paths.empty());

        options = rl::PenaltyOptions();
        options.penaltyFactor = 1.0;  // must be > 1
        assert(rl::penaltyAlternatives(g, &weights, 0, 4, options)
                   .paths.empty());

        options = rl::PenaltyOptions();
        assert(rl::penaltyAlternatives(g, &weights, 0, 9, options)
                   .paths.empty());

        std::vector<double> wrongSize(2, 1.0);
        assert(rl::penaltyAlternatives(g, wrongSize, 0, 4, options)
                   .paths.empty());

        Weights zero = {0, 0, 0, 0};
        assert(rl::penaltyAlternatives(g, &zero, 0, 4, options)
                   .paths.empty());
        graph_free(g);
    }

    std::cout << "All penalty method tests passed.\n";
    return 0;
}
