
#include "astar.hpp"
#include "min_heap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace rl {

AStarResult astar(
    const Graph* graph,
    const Weights* weights,
    const std::vector<LatLon>* positions,
    int source,
    int destination
) {
    AStarResult result;

    if (!graph || !weights || !positions ||
        graph->node_count <= 0 ||
        positions->size() != static_cast<std::size_t>(graph->node_count) ||
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

    // Safe for all supported distance, time, traffic and signal weights.
   // Zero heuristic makes A* behave like Dijkstra.
   const double INF = std::numeric_limits<double>::infinity();
   
   auto heuristic = [&](int /*node*/) -> double {
       return 0.0;
   };

    std::vector<double> gScore(n, INF);
    std::vector<int> parent(n, -1);

    BinaryMinHeap heap;

    gScore[source] = 0.0;
    heap.push(source, heuristic(source));

    while (!heap.empty()) {
        HeapNode current = heap.pop();
        int u = current.vertex;

        double expectedPriority = gScore[u] + heuristic(u);

        if (current.priority > expectedPriority + 1e-9) {
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

            double tentative = gScore[u] + cost;

            if (tentative < gScore[v]) {
                gScore[v] = tentative;
                parent[v] = u;

                heap.push(v, tentative + heuristic(v));
            }
        }
    }

    if (!std::isfinite(gScore[destination])) {
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
    result.total_cost = gScore[destination];
    result.path = std::move(reversedPath);

    return result;
}

} // namespace rl
