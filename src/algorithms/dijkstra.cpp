#include "dijkstra.hpp"
#include "min_heap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace rl {

DijkstraResult dijkstra(const Graph* graph,
                        const Weights* weights,
                        int source,
                        int destination) {
    DijkstraResult result;

    if (!graph || !weights || graph->node_count <= 0 ||
        source < 0 || destination < 0 ||
        source >= graph->node_count ||
        destination >= graph->node_count ||
        !weights_valid(weights)) {
        return result;
    }

    const int n = graph->node_count;
    const int m = graph->edge_count;

    std::vector<double> edgeCosts(m);
    if (m > 0 &&
        !graph_compute_costs(graph, weights, edgeCosts.data())) {
        return result;
    }

    const double INF = std::numeric_limits<double>::infinity();

    std::vector<double> distance(n, INF);
    std::vector<int> parent(n, -1);
    BinaryMinHeap heap;

    distance[source] = 0.0;
    heap.push(source, 0.0);

    while (!heap.empty()) {
        HeapNode current = heap.pop();
        int u = current.vertex;

        // Ignore outdated entries in the heap.
        if (current.priority > distance[u]) {
            continue;
        }

        if (u == destination) {
            break;
        }

        for (int edgeIndex = graph->first_out[u];
             edgeIndex != -1;
             edgeIndex = graph->next_out[edgeIndex]) {
            const Edge& edge = graph->edges[edgeIndex];
            int v = edge.to;
            double cost = edgeCosts[edgeIndex];

            if (!std::isfinite(cost) || cost < 0.0) {
                continue;
            }

            double newDistance = distance[u] + cost;

            if (newDistance < distance[v]) {
                distance[v] = newDistance;
                parent[v] = u;
                heap.push(v, newDistance);
            }
        }
    }

    if (!std::isfinite(distance[destination])) {
        return result;
    }

    std::vector<int> reversedPath;

    for (int v = destination; v != -1; v = parent[v]) {
        reversedPath.push_back(v);

        if (v == source) {
            break;
        }
    }

    if (reversedPath.empty() || reversedPath.back() != source) {
        return result;
    }

    std::reverse(reversedPath.begin(), reversedPath.end());

    result.found = true;
    result.total_cost = distance[destination];
    result.path = std::move(reversedPath);

    return result;
}

} // namespace rl
