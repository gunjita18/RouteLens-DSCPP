
#include "astar.hpp"
#include "min_heap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace rl {

namespace {

// Edge lengths are rounded to 0.01 m, so a road can be a hair shorter than the
// straight line between its endpoints. This margin keeps h a strict lower bound.
constexpr double kHeuristicSafety = 0.999;

} // namespace

double graphMaxSpeedMps(const Graph* graph) {
    if (!graph || graph->edge_count <= 0 || !graph->edges) {
        return 0.0;
    }

    double best = 0.0;

    for (int i = 0; i < graph->edge_count; ++i) {
        const Edge& edge = graph->edges[i];

        if (edge.duration_s > 0.0) {
            best = std::max(best, edge.distance_m / edge.duration_s);
        } else if (edge.distance_m > 0.0) {
            return std::numeric_limits<double>::infinity();
        }
    }

    return best;
}

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
        graph->edge_count < 0 ||
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

    return astarWithCosts(
        graph, edgeCosts, weights, positions, source, destination);
}

AStarResult astarWithCosts(
    const Graph* graph,
    const std::vector<double>& edgeCosts,
    const Weights* weights,
    const std::vector<LatLon>* positions,
    int source,
    int destination,
    double maxSpeedMps
) {
    AStarResult result;

    if (!graph || !weights || !positions ||
        graph->node_count <= 0 ||
        graph->edge_count < 0 ||
        positions->size() != static_cast<std::size_t>(graph->node_count) ||
        source < 0 || destination < 0 ||
        source >= graph->node_count ||
        destination >= graph->node_count ||
        !weights_valid(weights)) {
        return result;
    }

    const int n = graph->node_count;
    const int m = graph->edge_count;

    if (edgeCosts.size() != static_cast<std::size_t>(m)) {
        return result;
    }

    if (m > 0 && (!graph->edges || !graph->first_out ||
                  !graph->next_out)) {
        return result;
    }

    if (maxSpeedMps <= 0.0) {
        maxSpeedMps = graphMaxSpeedMps(graph);
    }

    const bool timeBound =
        std::isfinite(maxSpeedMps) && maxSpeedMps > 0.0;

    const double INF = std::numeric_limits<double>::infinity();
    const LatLon goal = (*positions)[destination];

    // Lower bound on the remaining cost from `node` to the destination.
    // The haversine is the expensive part, so each vertex is computed once.
    std::vector<double> heuristicCache(n, -1.0);  // h >= 0, so -1 = not yet computed

    auto heuristic = [&](int node) -> double {
        double& cached = heuristicCache[node];

        if (cached >= 0.0) {
            return cached;
        }

        const double meters =
            kHeuristicSafety * haversineMeters((*positions)[node], goal);

        double h = weights->per_km * (meters / 1000.0);

        if (timeBound) {
            h += weights->per_minute * (meters / maxSpeedMps / 60.0);
        }

        cached = h;
        return h;
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

            const Edge& edge = graph->edges[edgeIndex];
            int v = edge.to;
            double cost = edgeCosts[edgeIndex];

            if (v < 0 || v >= n ||
                !std::isfinite(cost) || cost < 0.0) {
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
