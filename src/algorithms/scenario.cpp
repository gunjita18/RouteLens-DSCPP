
#include "scenario.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "../core/bfs.h"

namespace rl {

namespace {

bool edgeNearCenter(
    const Edge& edge,
    int edgeIndex,
    const std::vector<LatLon>& positions,
    const std::vector<std::vector<LatLon>>* geometry,
    const Closure& closure
) {
    if (geometry &&
        static_cast<std::size_t>(edgeIndex) < geometry->size() &&
        (*geometry)[edgeIndex].size() >= 2) {
        return pointToPolylineMeters(
                   closure.center, (*geometry)[edgeIndex]) <=
               closure.radiusM;
    }

    return pointToSegmentMeters(
               closure.center, positions[edge.from], positions[edge.to]) <=
           closure.radiusM;
}

} // namespace

ScenarioEffect applyClosures(
    const Graph* graph,
    const std::vector<LatLon>& positions,
    const std::vector<std::vector<LatLon>>* geometry,
    const std::vector<Closure>& closures
) {
    ScenarioEffect effect;

    if (!graph || graph->edge_count < 0 ||
        positions.size() < static_cast<std::size_t>(graph->node_count)) {
        return effect;
    }

    const int m = graph->edge_count;

    effect.blocked.assign(m, false);
    effect.durationFactor.assign(m, 1.0);
    effect.edgesPerClosure.assign(closures.size(), 0);

    for (std::size_t c = 0; c < closures.size(); ++c) {
        const Closure& closure = closures[c];

        if (!std::isfinite(closure.radiusM) || closure.radiusM < 0.0 ||
            (!closure.block &&
             (!std::isfinite(closure.slowFactor) ||
              closure.slowFactor < 1.0))) {
            continue;
        }

        for (int e = 0; e < m; ++e) {
            const Edge& edge = graph->edges[e];

            if (edge.from < 0 || edge.from >= graph->node_count ||
                edge.to < 0 || edge.to >= graph->node_count) {
                continue;
            }

            if (!edgeNearCenter(edge, e, positions, geometry, closure)) {
                continue;
            }

            ++effect.edgesPerClosure[c];

            if (closure.block) {
                effect.blocked[e] = true;
            } else {
                effect.durationFactor[e] *= closure.slowFactor;
            }
        }
    }

    for (int e = 0; e < m; ++e) {
        if (effect.blocked[e]) {
            ++effect.blockedEdges;
        } else if (effect.durationFactor[e] > 1.0) {
            ++effect.slowedEdges;
        }
    }

    return effect;
}

std::vector<double> scenarioCosts(
    const Graph* graph,
    const Weights* weights,
    const ScenarioEffect& effect
) {
    if (!graph || !weights || !weights_valid(weights) ||
        graph->edge_count < 0 ||
        effect.durationFactor.size() !=
            static_cast<std::size_t>(graph->edge_count)) {
        return {};
    }

    std::vector<double> costs(graph->edge_count);

    for (int i = 0; i < graph->edge_count; ++i) {
        const Edge& e = graph->edges[i];
        const double km = e.distance_m / 1000.0;
        const double minutes =
            e.duration_s / 60.0 * effect.durationFactor[i];

        costs[i] = weights->per_km * km
                 + weights->per_minute * minutes
                 + weights->per_congested_minute * e.traffic_index * minutes
                 + weights->per_signal * static_cast<double>(e.signal_count);
    }

    return costs;
}

ReachResult bfsReachable(
    const Graph* graph,
    int source,
    int destination,
    const std::vector<bool>* blockedEdges
) {
    ReachResult result;

    if (!graph || graph->node_count <= 0 || graph->edge_count < 0 ||
        source < 0 || destination < 0 ||
        source >= graph->node_count ||
        destination >= graph->node_count) {
        return result;
    }

    if (blockedEdges &&
        blockedEdges->size() != static_cast<std::size_t>(graph->edge_count)) {
        return result;
    }

    // The search itself is the hand-written C BFS with the shared circular queue
    // (src/core/bfs.c, bfs_hops). std::vector<bool> is packed bits, so the
    // closures are copied into one char per edge for C.
    std::vector<char> blocked;
    if (blockedEdges) blocked.assign(blockedEdges->begin(), blockedEdges->end());

    int visited = 0;
    const int hops = bfs_hops(graph, source, destination,
                              blocked.empty() ? nullptr : blocked.data(), &visited);
    if (hops == BFS_ERROR) return result;   // out of memory: reported as unreachable

    result.visited = visited;
    if (hops >= 0) {
        result.reachable = true;
        result.hops = hops;
    }
    return result;
}

} // namespace rl
