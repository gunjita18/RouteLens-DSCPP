
#include "../src/algorithms/dijkstra.hpp"

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

    auto result = rl::dijkstra(graph, &weights, 0, 3);

    assert(result.found);
    assert((result.path == std::vector<int>{0, 1, 2, 3}));
    assert(std::abs(result.total_cost - 3.0) < 1e-9);

    auto same = rl::dijkstra(graph, &weights, 2, 2);
    assert(same.found);
    assert((same.path == std::vector<int>{2}));
    assert(std::abs(same.total_cost) < 1e-9);

    auto invalid = rl::dijkstra(graph, &weights, 0, 4);
    assert(!invalid.found);

    graph_free(graph);

    std::cout << "All Dijkstra tests passed.\n";
    return 0;
}

