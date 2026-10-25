
#include "../src/algorithms/astar.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    Graph* graph = graph_create(4);
    assert(graph != nullptr);

    graph_add_edge(graph, 0, 1, 1000, 0, 0, nullptr, 0);
    graph_add_edge(graph, 0, 2, 4000, 0, 0, nullptr, 0);
    graph_add_edge(graph, 1, 2, 1000, 0, 0, nullptr, 0);
    graph_add_edge(graph, 1, 3, 5000, 0, 0, nullptr, 0);
    graph_add_edge(graph, 2, 3, 1000, 0, 0, nullptr, 0);

    Weights weights = {1.0, 0.0, 0.0, 0.0};

    // Positions are arranged so the heuristic remains admissible.
    std::vector<rl::LatLon> positions = {
        {30.000, 78.000},
        {30.001, 78.000},
        {30.002, 78.000},
        {30.003, 78.000}
    };

    auto result = rl::astar(&graph[0], &weights, &positions, 0, 3);

    assert(result.found);
    assert((result.path == std::vector<int>{0, 1, 2, 3}));
    assert(std::abs(result.total_cost - 3.0) < 1e-9);

    auto same = rl::astar(graph, &weights, &positions, 2, 2);
    assert(same.found);
    assert((same.path == std::vector<int>{2}));
    assert(std::abs(same.total_cost) < 1e-9);

    auto invalid = rl::astar(graph, &weights, &positions, 0, 4);
    assert(!invalid.found);

    graph_free(graph);

    std::cout << "All A* tests passed.\n";
    return 0;
}
