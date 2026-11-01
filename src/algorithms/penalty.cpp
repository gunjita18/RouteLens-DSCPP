
#include "penalty.hpp"
#include "dijkstra.hpp"

#include <cmath>
#include <set>
#include <utility>
#include <vector>

namespace rl {

PenaltyResult penaltyAlternatives(
    const Graph* graph,
    const std::vector<double>& baseCosts,
    int source,
    int destination,
    const PenaltyOptions& options,
    const std::vector<bool>* blockedEdges
) {
    PenaltyResult result;

    if (!graph || graph->node_count <= 0 || graph->edge_count < 0 ||
        options.k <= 0 ||
        !std::isfinite(options.penaltyFactor) ||
        options.penaltyFactor <= 1.0 ||
        !std::isfinite(options.maxStretch) || options.maxStretch < 0.0 ||
        options.maxIterations < 0 ||
        baseCosts.size() != static_cast<std::size_t>(graph->edge_count)) {
        return result;
    }

    const int maxIterations =
        options.maxIterations > 0 ? options.maxIterations : 5 * options.k;

    // A zero-cost edge would ignore a purely multiplicative penalty, so every
    // penalty also adds a small floor relative to the typical edge cost (a tiny
    // constant when every edge is free).
    double positiveSum = 0.0;
    int positiveCount = 0;
    for (double c : baseCosts) {
        if (std::isfinite(c) && c > 0.0) {
            positiveSum += c;
            ++positiveCount;
        }
    }
    const double floorPenalty =
        positiveCount > 0 ? 0.001 * positiveSum / positiveCount : 1e-9;

    std::vector<double> costs = baseCosts;
    std::set<std::vector<int>> seen;
    double shortestCost = 0.0;

    for (int iteration = 0;
         iteration < maxIterations &&
         static_cast<int>(result.paths.size()) < options.k;
         ++iteration) {

        DijkstraResult found = dijkstraWithCosts(
            graph, costs, source, destination, blockedEdges);
        ++result.iterations;

        if (!found.found) {
            break;
        }

        // source == destination: the only route is the zero-edge path.
        if (found.edge_path.empty()) {
            result.paths.push_back(found.path);
            result.edge_paths.push_back(found.edge_path);
            result.total_costs.push_back(0.0);
            break;
        }

        // Penalise first, so the next search cannot repeat this route even
        // when it is rejected below.
        for (int e : found.edge_path) {
            costs[e] = costs[e] * options.penaltyFactor + floorPenalty;
        }

        if (!seen.insert(found.path).second) {
            continue;
        }

        double trueCost = 0.0;
        for (int e : found.edge_path) {
            trueCost += baseCosts[e];
        }

        if (result.paths.empty()) {
            shortestCost = trueCost;
        } else if (options.maxStretch > 0.0 &&
                   trueCost > options.maxStretch * shortestCost) {
            continue;
        }

        result.paths.push_back(std::move(found.path));
        result.edge_paths.push_back(std::move(found.edge_path));
        result.total_costs.push_back(trueCost);
    }

    return result;
}

PenaltyResult penaltyAlternatives(
    const Graph* graph,
    const Weights* weights,
    int source,
    int destination,
    const PenaltyOptions& options
) {
    PenaltyResult result;

    if (!graph || !weights || graph->node_count <= 0 ||
        graph->edge_count < 0 || !weights_valid(weights)) {
        return result;
    }

    const int m = graph->edge_count;
    std::vector<double> costs(m);

    if (m > 0 && (!graph->edges || !graph->first_out ||
                  !graph->next_out ||
                  !graph_compute_costs(graph, weights, costs.data()))) {
        return result;
    }

    return penaltyAlternatives(
        graph, costs, source, destination, options, nullptr);
}

} // namespace rl
