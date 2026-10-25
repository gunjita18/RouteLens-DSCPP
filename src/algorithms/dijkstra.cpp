
#include "dijkstra.hpp"
#include "min_heap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace rl {

DijkstraResult dijkstra(
    const Graph* graph,
    const Weights* weights,
    int source,
    int destination,
    const std::vector<bool>* blockedEdges
) {
    DijkstraResult result;

    if (!graph || !weights || graph->node_count <= 0 ||
        graph->edge_count < 0 ||
        source < 0 || destination < 0 ||
        source >= graph->node_count ||
        destination >= graph->node_count ||
        !weights_valid(weights)) {
        return result;
    }

    const int m = graph->edge_count;

    if (m > 0 && (!graph->edges || !graph->first_out ||
                  !graph->next_out)) {
        return result;
    }

    std::vector<double> edgeCosts(m);

    if (m > 0 &&
        !graph_compute_costs(graph, weights, edgeCosts.data())) {
        return result;
    }

    return dijkstraWithCosts(
        graph, edgeCosts, source, destination, blockedEdges);
}

DijkstraResult dijkstraWithCosts(
    const Graph* graph,
    const std::vector<double>& edgeCosts,
    int source,
    int destination,
    const std::vector<bool>* blockedEdges
) {
    DijkstraResult result;

    if (!graph || graph->node_count <= 0 ||
        graph->edge_count < 0 ||
        source < 0 || destination < 0 ||
        source >= graph->node_count ||
        destination >= graph->node_count) {
        return result;
    }

    const int n = graph->node_count;
    const int m = graph->edge_count;

    if (edgeCosts.size() != static_cast<std::size_t>(m)) {
        return result;
    }

    if (blockedEdges && blockedEdges->size() !=
                            static_cast<std::size_t>(m)) {
        return result;
    }

    if (m > 0 && (!graph->edges || !graph->first_out ||
                  !graph->next_out)) {
        return result;
    }

    const double INF = std::numeric_limits<double>::infinity();

    std::vector<double> distance(n, INF);
    std::vector<int> parent(n, -1);
    std::vector<int> parentEdge(n, -1);

    BinaryMinHeap heap;

    distance[source] = 0.0;
    heap.push(source, 0.0);

    while (!heap.empty()) {
        HeapNode current = heap.pop();
        int u = current.vertex;

        if (current.priority > distance[u]) {
            continue;
        }

        ++result.nodes_expanded;

        if (u == destination) {
            break;
        }

        for (int edgeIndex = graph->first_out[u];
             edgeIndex != -1;
             edgeIndex = graph->next_out[edgeIndex]) {

            if (edgeIndex < 0 || edgeIndex >= m) {
                result.nodes_expanded = 0;
                return result;
            }

            if (blockedEdges &&
                (*blockedEdges)[edgeIndex]) {
                continue;
            }

            const Edge& edge = graph->edges[edgeIndex];
            int v = edge.to;
            double cost = edgeCosts[edgeIndex];

            if (v < 0 || v >= n ||
                !std::isfinite(cost) || cost < 0.0) {
                continue;
            }

            double newDistance = distance[u] + cost;

            if (newDistance < distance[v]) {
                distance[v] = newDistance;
                parent[v] = u;
                parentEdge[v] = edgeIndex;
                heap.push(v, newDistance);
            }
        }
    }

    if (!std::isfinite(distance[destination])) {
        return result;
    }

    std::vector<int> reversedPath;
    std::vector<int> reversedEdges;
    int v = destination;

    while (v != source) {
        if (v < 0 || v >= n || parent[v] == -1 ||
            parentEdge[v] == -1) {
            return result;
        }

        reversedPath.push_back(v);
        reversedEdges.push_back(parentEdge[v]);
        v = parent[v];

        if (reversedPath.size() > static_cast<std::size_t>(n)) {
            return result;
        }
    }

    reversedPath.push_back(source);
    std::reverse(reversedPath.begin(), reversedPath.end());
    std::reverse(reversedEdges.begin(), reversedEdges.end());

    result.found = true;
    result.total_cost = distance[destination];
    result.path = std::move(reversedPath);
    result.edge_path = std::move(reversedEdges);

    return result;
}

} // namespace rl
