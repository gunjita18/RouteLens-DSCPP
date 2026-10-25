#include "osm_route_provider.hpp"

#include "../algorithms/penalty.hpp"
#include "../algorithms/scenario.hpp"
#include "../algorithms/yen.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
#include <set>
#include <vector>

namespace rl {
namespace {

struct Candidate {
    RouteInfo route;
    double rawDistance = 0.0;
    double rawEta = 0.0;
    double rawTraffic = 0.0;
    std::vector<int> path;
};

bool validNode(const OsmNetwork& net, int node) {
    return net.graph && node >= 0 && node < net.graph->node_count;
}

double clampTraffic(double value) {
    if (!std::isfinite(value)) return 0.0;
    return std::clamp(value, 0.0, 1.0);
}

void appendPoint(RouteInfo& route, const LatLon& point) {
    if (!std::isfinite(point.lat) || !std::isfinite(point.lon)) return;

    if (!route.polyline.empty()) {
        const auto& last = route.polyline.back();
        if (std::abs(last.first - point.lat) < 1e-8 &&
            std::abs(last.second - point.lon) < 1e-8) {
            return;
        }
    }

    route.polyline.emplace_back(point.lat, point.lon);
}

// Find the directed edge a search would use between two consecutive vertices:
// the cheapest usable one when parallel edges exist.
int findBestEdge(const Graph* graph, const std::vector<double>& costs,
                 const std::vector<bool>& blocked, int from, int to) {
    if (!graph || from < 0 || from >= graph->node_count) return -1;

    int best = -1;
    for (int e = graph->first_out[from]; e != -1; e = graph->next_out[e]) {
        if (graph->edges[e].to != to) continue;
        if (!blocked.empty() && blocked[e]) continue;
        if (best < 0 || costs[e] < costs[best]) best = e;
    }

    return best;
}

double normalise(double value, double minimum, double maximum) {
    if (maximum <= minimum) return 0.0;
    return (value - minimum) / (maximum - minimum);
}

// Treat a road segment as the same segment in either direction.
std::vector<std::pair<int, int>> undirectedEdges(
    const std::vector<int>& path) {
    std::vector<std::pair<int, int>> edges;
    if (path.size() < 2) return edges;

    for (std::size_t i = 1; i < path.size(); ++i) {
        const int a = std::min(path[i - 1], path[i]);
        const int b = std::max(path[i - 1], path[i]);
        edges.emplace_back(a, b);
    }

    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    return edges;
}

// Fraction of the shorter route's road segments shared by both routes.
double overlapRatio(const std::vector<int>& first,
                    const std::vector<int>& second) {
    const auto a = undirectedEdges(first);
    const auto b = undirectedEdges(second);

    if (a.empty() || b.empty()) return 0.0;

    std::vector<std::pair<int, int>> common;
    std::set_intersection(
        a.begin(), a.end(), b.begin(), b.end(),
        std::back_inserter(common));

    const std::size_t denominator = std::min(a.size(), b.size());
    return denominator == 0
        ? 0.0
        : static_cast<double>(common.size()) /
              static_cast<double>(denominator);
}

}  // namespace

OsmRouteProvider::OsmRouteProvider(const OsmNetwork& network)
    : network_(network) {
    const Graph* graph = network_.graph;
    if (!graph) return;

    degree_.assign(graph->node_count, 0);

    for (int i = 0; i < graph->edge_count; ++i) {
        const Edge& edge = graph->edges[i];
        if (edge.from >= 0 && edge.from < graph->node_count) {
            ++degree_[edge.from];
        }
        if (edge.to >= 0 && edge.to < graph->node_count) {
            ++degree_[edge.to];
        }
    }
}

bool OsmRouteProvider::available() const {
    return network_.graph != nullptr &&
           network_.pos.size() >=
               static_cast<std::size_t>(network_.graph->node_count);
}

std::vector<RouteInfo> OsmRouteProvider::compute(
    const RouteRequest& request) {

    std::vector<RouteInfo> result;

    const Graph* graph = network_.graph;
    if (!available() ||
        !validNode(network_, request.fromNode) ||
        !validNode(network_, request.toNode) ||
        request.fromNode == request.toNode) {
        return result;
    }

    // Yen's path-generation costs combine distance, free-flow time,
    // traffic index and mapped signals. These are candidate-generation
    // weights; final ranking below normalises route metrics separately.
    Weights weights{};
    weights.per_km = request.wDistance;
    weights.per_minute = request.wTime;
    weights.per_congested_minute = request.wTraffic;
    weights.per_signal = request.wJunctions;

    // What-if closures (empty for a normal request).
    const bool hasScenario =
        !request.blockedEdges.empty() || !request.durationFactor.empty();
    const bool validScenario =
        (request.blockedEdges.empty() ||
         request.blockedEdges.size() ==
             static_cast<std::size_t>(graph->edge_count)) &&
        (request.durationFactor.empty() ||
         request.durationFactor.size() ==
             static_cast<std::size_t>(graph->edge_count));
    if (!validScenario) return result;

    ScenarioEffect effect;
    effect.blocked = request.blockedEdges;
    effect.durationFactor = request.durationFactor;
    if (effect.durationFactor.empty()) {
        effect.durationFactor.assign(graph->edge_count, 1.0);
    }

    const std::vector<double> edgeCosts =
        scenarioCosts(graph, &weights, effect);
    if (edgeCosts.size() != static_cast<std::size_t>(graph->edge_count)) {
        return result;
    }
    const std::vector<bool>* blocked =
        request.blockedEdges.empty() ? nullptr : &request.blockedEdges;

    // The penalty method is the primary generator of alternatives. Yen's
    // k-shortest paths tops the list up with near-shortest variants; it works
    // on the unmodified graph, so it is skipped when closures are active.
    PenaltyOptions penalty;
    penalty.k = 5;
    penalty.penaltyFactor = 1.5;
    penalty.maxStretch = 1.5;
    const PenaltyResult penaltyRoutes = penaltyAlternatives(
        graph, edgeCosts, request.fromNode, request.toNode, penalty, blocked);

    std::vector<std::vector<int>> generatedPaths = penaltyRoutes.paths;

    if (!hasScenario) {
        const YenResult alternatives =
            yenKShortestPaths(graph, &weights, request.fromNode,
                             request.toNode, 8);
        std::set<std::vector<int>> known(generatedPaths.begin(),
                                         generatedPaths.end());
        for (const auto& path : alternatives.paths) {
            if (known.insert(path).second) generatedPaths.push_back(path);
        }
    }

    std::vector<Candidate> candidates;

    for (const auto& path : generatedPaths) {
        if (path.size() < 2) continue;

        Candidate candidate;
        candidate.path = path;
        RouteInfo& route = candidate.route;
        route.name = "Alternative route";
        double durationSeconds = 0.0;
        double trafficWeightedSeconds = 0.0;
        double totalDistanceM = 0.0;
        double totalTrafficWeight = 0.0;

        appendPoint(route, network_.pos[path.front()]);

        bool validPath = true;

        for (std::size_t i = 1; i < path.size(); ++i) {
            const int from = path[i - 1];
            const int to = path[i];

            const int edgeIndex = findBestEdge(
                graph, edgeCosts, request.blockedEdges, from, to);
            if (edgeIndex < 0) {
                validPath = false;
                break;
            }

            const Edge& edge = graph->edges[edgeIndex];
            // Slowed roads take longer; 1.0 when there is no scenario.
            const double edgeSeconds = std::max(0.0, edge.duration_s) *
                                       effect.durationFactor[edgeIndex];

            totalDistanceM += std::max(0.0, edge.distance_m);
            durationSeconds += edgeSeconds;

            const double traffic = clampTraffic(edge.traffic_index);
            trafficWeightedSeconds += traffic * edgeSeconds;
            totalTrafficWeight += edgeSeconds;

            // Count internal graph nodes with more than one connected
            // road in the degree approximation used by this provider.
            if (i + 1 < path.size() && to >= 0 &&
                static_cast<std::size_t>(to) < degree_.size() &&
                degree_[to] > 2) {
                ++route.junctions;
            }

            if (edgeIndex >= 0 &&
                static_cast<std::size_t>(edgeIndex) <
                    network_.geometry.size() &&
                !network_.geometry[edgeIndex].empty()) {

                std::vector<LatLon> shape = network_.geometry[edgeIndex];

                // Ensure the shape follows the directed edge orientation.
                const LatLon& fromPos = network_.pos[from];
                const LatLon& toPos = network_.pos[to];

                if (haversineMeters(shape.front(), toPos) <
                    haversineMeters(shape.front(), fromPos)) {
                    std::reverse(shape.begin(), shape.end());
                }

                for (const auto& point : shape) {
                    appendPoint(route, point);
                }
            } else {
                appendPoint(route, network_.pos[to]);
            }
        }

        if (!validPath || totalDistanceM <= 0.0 ||
            route.polyline.size() < 2) {
            continue;
        }

        candidate.rawDistance = totalDistanceM / 1000.0;
        candidate.rawTraffic =
            totalTrafficWeight > 0.0
                ? trafficWeightedSeconds / totalTrafficWeight
                : 0.0;
        candidate.rawEta = durationSeconds +
                           static_cast<double>(route.junctions) * 20.0;

        route.distanceKm = candidate.rawDistance;
        route.freeFlowEtaSeconds = candidate.rawEta;
        route.etaSeconds = candidate.rawEta;

        // No live or historical traffic is applied by this provider yet.
        // Do not label the route as live traffic.
        route.livePct = 0.0;
        route.typicalPct = 0.0;
        route.estimatedPct = 100.0;
        route.trafficLevel = "Estimated";

        candidates.push_back(std::move(candidate));
    }

    if (candidates.empty()) return result;

    // Reject excessive detours and alternatives that reuse almost all
    // of the shorter route's road segments. Keep at most three routes.
    double shortestDistance = std::numeric_limits<double>::infinity();
    for (const auto& candidate : candidates) {
        shortestDistance =
            std::min(shortestDistance, candidate.rawDistance);
    }

    constexpr double maxDetourRatio = 1.50;
    constexpr double maxSharedOverlap = 0.85;
    constexpr std::size_t maxRoutes = 3;

    std::vector<Candidate> diverseCandidates;
    diverseCandidates.reserve(maxRoutes);

    for (auto& candidate : candidates) {
        if (candidate.rawDistance >
            shortestDistance * maxDetourRatio) {
            std::cerr << "[RouteLens] Rejected: detour too long\n";
            continue;
        }

        bool tooSimilar = false;
        for (const auto& accepted : diverseCandidates) {
            const double overlap =
                overlapRatio(candidate.path, accepted.path);

            std::cerr << "[RouteLens] Overlap with accepted route: "
                      << overlap * 100.0 << "%\n";

            if (overlap >= maxSharedOverlap) {
                std::cerr << "[RouteLens] Rejected: too much overlap\n";
                tooSimilar = true;
                break;
            }
        }

        if (!tooSimilar) {
            diverseCandidates.push_back(std::move(candidate));
            if (diverseCandidates.size() == maxRoutes) break;
        }
    }

    candidates = std::move(diverseCandidates);
    if (candidates.empty()) return result;

    double minDistance = std::numeric_limits<double>::infinity();
    double maxDistance = 0.0;
    double minEta = std::numeric_limits<double>::infinity();
    double maxEta = 0.0;
    double minJunctions = std::numeric_limits<double>::infinity();
    double maxJunctions = 0.0;
    double minTraffic = std::numeric_limits<double>::infinity();
    double maxTraffic = 0.0;

    for (const auto& candidate : candidates) {
        minDistance = std::min(minDistance, candidate.rawDistance);
        maxDistance = std::max(maxDistance, candidate.rawDistance);
        minEta = std::min(minEta, candidate.rawEta);
        maxEta = std::max(maxEta, candidate.rawEta);

        const double junctions =
            static_cast<double>(candidate.route.junctions);
        minJunctions = std::min(minJunctions, junctions);
        maxJunctions = std::max(maxJunctions, junctions);

        minTraffic = std::min(minTraffic, candidate.rawTraffic);
        maxTraffic = std::max(maxTraffic, candidate.rawTraffic);
    }

    for (auto& candidate : candidates) {
        const double score =
            request.wDistance *
                normalise(candidate.rawDistance, minDistance, maxDistance) +
            request.wTime *
                normalise(candidate.rawEta, minEta, maxEta) +
            request.wJunctions *
                normalise(static_cast<double>(candidate.route.junctions),
                          minJunctions, maxJunctions) +
            request.wTraffic *
                normalise(candidate.rawTraffic, minTraffic, maxTraffic);

        candidate.route.score = score;
    }

    std::stable_sort(
        candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) {
            return a.route.score < b.route.score;
        });

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        candidates[i].route.name =
            "Route " + std::to_string(i + 1);
        candidates[i].route.recommended = (i == 0);
        result.push_back(std::move(candidates[i].route));
    }

    return result;
}

}  // namespace rl
