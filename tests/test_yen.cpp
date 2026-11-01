
#include "../src/algorithms/yen.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <set>
#include <vector>

int main() {
    Graph* graph = graph_create(5);
    assert(graph != nullptr);

    // Three alternative routes from 0 to 4:
    //
    // 0 -> 1 -> 4 : cost 2
    // 0 -> 2 -> 4 : cost 3
    // 0 -> 3 -> 4 : cost 4

    assert(graph_add_edge(graph, 0, 1, 1000, 0, 0, nullptr, 0) >= 0);
    assert(graph_add_edge(graph, 1, 4, 1000, 0, 0, nullptr, 0) >= 0);

    assert(graph_add_edge(graph, 0, 2, 1500, 0, 0, nullptr, 0) >= 0);
    assert(graph_add_edge(graph, 2, 4, 1500, 0, 0, nullptr, 0) >= 0);

    assert(graph_add_edge(graph, 0, 3, 2000, 0, 0, nullptr, 0) >= 0);
    assert(graph_add_edge(graph, 3, 4, 2000, 0, 0, nullptr, 0) >= 0);

    Weights weights = {
        1.0,  // cost per kilometre
        0.0,  // cost per minute
        0.0,  // cost per congested minute
        0.0   // cost per signal
    };

    rl::YenResult result =
        rl::yenKShortestPaths(graph, &weights, 0, 4, 3);

    assert(result.paths.size() == 3);
    assert(result.total_costs.size() == 3);

    assert(result.paths[0] == std::vector<int>({0, 1, 4}));
    assert(result.paths[1] == std::vector<int>({0, 2, 4}));
    assert(result.paths[2] == std::vector<int>({0, 3, 4}));

    assert(std::abs(result.total_costs[0] - 2.0) < 1e-9);
    assert(std::abs(result.total_costs[1] - 3.0) < 1e-9);
    assert(std::abs(result.total_costs[2] - 4.0) < 1e-9);

    // Requesting more routes than exist returns only available routes.
    rl::YenResult extra =
        rl::yenKShortestPaths(graph, &weights, 0, 4, 10);

    assert(extra.paths.size() == 3);

    // Invalid k returns no routes.
    rl::YenResult invalid =
        rl::yenKShortestPaths(graph, &weights, 0, 4, 0);

    assert(invalid.paths.empty());

    graph_free(graph);

    std::cout << "All Yen tests passed!\n";
    return 0;
}
