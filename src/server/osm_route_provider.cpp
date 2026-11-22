#include "osm_route_provider.hpp"

#include "../algorithms/scenario.hpp"
#include "../core/penalty.h"
#include "../core/sort.h"
#include "../oop/route.hpp"
#include "../oop/route_finder.hpp"
#include "../oop/scenario.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
#include <set>
#include <vector>

namespace rl {
namespace {

// Wait at each documented major junction a route passes (docs/TRAFFIC.md
// section 8: 30 s, provisional until the ETA accuracy study).
constexpr double kJunctionWaitS = 30.0;

// The fastest route is shown even when it shares most roads with another
// route, if it is at least this much faster.
constexpr double kDistinctEtaS = 30.0;

// Speed factor of a traffic scenario on one edge (docs/TRAFFIC.md section 13):
// 1 = unchanged. Normal and Incident leave speeds alone here; an incident is a
// road the user blocks or slows with a closure.
double scenarioSpeed(ScenarioKind scenario, const ScenarioSettings& settings,
                     const std::string& roadClass) {
    if (scenario == ScenarioKind::Rain) return settings.rainFactor;
    if (scenario == ScenarioKind::RushHour) {
        return isMainRoad(roadClass) ? settings.rushMainFactor
                                     : settings.rushOtherFactor;
    }
    return 1.0;
}

struct Candidate {
    RouteInfo route;
    double rawDistance = 0.0;
    double rawEta = 0.0;
    double rawTraffic = 0.0;
    std::vector<int> path;
};

// One route's place in the ranking, as a plain struct the C merge sort can copy.
struct RankItem {
    double score;   // lower is better
    int position;   // index into the candidate list
};

// Comparator in qsort style for merge_sort: by score, lower first.
int compareRankItems(const void* a, const void* b) {
    const double sa = static_cast<const RankItem*>(a)->score;
    const double sb = static_cast<const RankItem*>(b)->score;
    return (sa > sb) - (sa < sb);
}

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
    // Values equal up to rounding (e.g. rain slows every route by the same
    // factor) must not be stretched to 0 and 1 and decide the ranking.
    if (maximum - minimum <= 1e-9 * std::max(1.0, std::abs(maximum))) return 0.0;
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

OsmRouteProvider::OsmRouteProvider(const OsmNetwork& network,
                                   const JunctionIndex* junctions,
                                   const TrafficProvider* traffic)
    : network_(network), junctions_(junctions) {
    if (traffic == nullptr || network.graph == nullptr) return;

    // Read the measured traffic once. Travel time with data is length / measured
    // speed (the design's rule); a measured speed of 0 marks a closed road in the
    // data, which keeps the free-flow time (as TrafficState does) rather than
    // making the edge infinitely slow.
    const int edges = network.graph->edge_count;
    snapshotFactor_.assign(edges, 1.0);
    snapshotIndex_.assign(edges, 0.0);
    snapshotHasData_.assign(edges, 0);
    for (int e = 0; e < edges; ++e) {
        if (!traffic->hasData(e)) continue;
        const Edge& edge = network.graph->edges[e];
        const double kph = traffic->currentKph(e);
        const bool closed = kph <= 0.0;
        const double freeFlowS = std::max(0.0, edge.duration_s);
        if (!closed && freeFlowS > 0.0) {
            const double seconds = std::max(0.0, edge.distance_m) / (kph / 3.6);
            snapshotFactor_[e] = seconds / freeFlowS;
        }
        snapshotIndex_[e] = closed ? 1.0 : clampTraffic(traffic->trafficIndex(e));
        snapshotHasData_[e] = 1;
        ++edgesWithTraffic_;
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

    // Path-generation costs combine distance, travel time, traffic index and
    // junction waits. These are candidate-generation weights; final ranking
    // below normalises route metrics separately.
    Weights weights{};
    weights.per_km = request.wDistance;
    weights.per_minute = request.wTime;
    weights.per_congested_minute = request.wTraffic;
    weights.per_signal = request.wJunctions;

    // What-if closures (empty for a normal request).
    const bool validScenario =
        (request.blockedEdges.empty() ||
         request.blockedEdges.size() ==
             static_cast<std::size_t>(graph->edge_count)) &&
        (request.durationFactor.empty() ||
         request.durationFactor.size() ==
             static_cast<std::size_t>(graph->edge_count));
    if (!validScenario) return result;

    // Closures are physical changes and count in every ETA shown.
    std::vector<double> closureFactor = request.durationFactor;
    if (closureFactor.empty()) {
        closureFactor.assign(graph->edge_count, 1.0);
    }

    // Traffic scenario speeds per edge. Computed here for each request rather
    // than with TrafficState, which writes into the shared graph while Crow
    // serves requests in parallel. The server has validated the name already;
    // anything else counts as normal.
    ScenarioKind trafficScenario = ScenarioKind::Normal;
    parseScenario(request.scenario, trafficScenario);
    const ScenarioSettings scenarioSettings;
    std::vector<double> speed(graph->edge_count, 1.0);
    for (int e = 0; e < graph->edge_count; ++e) {
        const std::string roadClass =
            static_cast<std::size_t>(e) < network_.roadClass.size()
                ? network_.roadClass[e] : std::string();
        speed[e] = scenarioSpeed(trafficScenario, scenarioSettings, roadClass);
    }

    // Measured traffic (the saved snapshot) applies to the scenarios that build
    // on current conditions; rain and rush hour start from free flow instead, so a
    // slowdown is never counted twice.
    const bool useSnapshot =
        edgesWithTraffic_ > 0 && scenarioNeedsLiveTraffic(trafficScenario);

    ScenarioEffect effect;
    effect.blocked = request.blockedEdges;
    effect.durationFactor.resize(graph->edge_count);
    for (int e = 0; e < graph->edge_count; ++e) {
        effect.durationFactor[e] =
            closureFactor[e] * (useSnapshot ? snapshotFactor_[e] : 1.0) / speed[e];
    }

    // The search must see what the ETA counts: congestion from the traffic
    // scenario and the wait at each major junction. A divided junction has
    // several nodes, so the wait is charged only on an edge that enters the
    // junction from outside it, as the ETA counts it once per route.
    auto costsFor = [&](const Weights& w) {
        std::vector<double> costs = scenarioCosts(graph, &w, effect);
        if (costs.size() != static_cast<std::size_t>(graph->edge_count)) {
            return std::vector<double>();
        }
        for (int e = 0; e < graph->edge_count; ++e) {
            const Edge& edge = graph->edges[e];
            const double minutes =
                std::max(0.0, edge.duration_s) / 60.0 * effect.durationFactor[e];
            costs[e] += w.per_congested_minute * (1.0 - speed[e]) * minutes;
            if (useSnapshot) {
                costs[e] += w.per_congested_minute * snapshotIndex_[e] * minutes;
            }
            if (junctions_) {
                const int j = junctions_->junctionOf(edge.to);
                if (j >= 0 && j != junctions_->junctionOf(edge.from)) {
                    costs[e] += w.per_minute * kJunctionWaitS / 60.0 + w.per_signal;
                }
            }
        }
        return costs;
    };

    const std::vector<double> edgeCosts = costsFor(weights);
    if (edgeCosts.empty()) return result;

    // Routes are generated by the hand-written C routing core (src/core/),
    // reached through the RouteFinder classes (src/oop/route_finder.hpp): a
    // PenaltyRouter for the alternatives and a DijkstraRouter for the fastest
    // route. Both need finite, non-negative edge costs, which costsFor()
    // guarantees (every term is a non-negative weight times a non-negative number).
    const std::vector<char> blockedMask(request.blockedEdges.begin(),
                                        request.blockedEdges.end());

    // Runs any RouteFinder and turns its routes into vertex paths, which the rest
    // of this function works with. Called through the base class (polymorphism).
    const auto pathsFrom = [&](const RouteFinder& finder) {
        std::vector<std::vector<int>> paths;
        for (const Route& route : finder.find(request.fromNode, request.toNode)) {
            paths.push_back(route.vertices(graph, request.fromNode));
        }
        return paths;
    };

    // Penalty method (docs/C_ROUTING_CORE.md): the edges of each route found cost
    // 1.4 times more for the next search; a route is dropped when more than 70 %
    // of its edges belong to an earlier route, or when it costs over 1.5 times the
    // cheapest route.
    ::PenaltyOptions penalty = penalty_default_options();
    penalty.k = 5;
    penalty.max_stretch = 1.5;
    const PenaltyRouter alternatives(graph, edgeCosts, blockedMask, penalty);
    std::vector<std::vector<int>> generatedPaths = pathsFrom(alternatives);

    // The fastest route by the ETA shown, whatever the priorities. Without it
    // the "fastest" route listed could be beaten by one the weighted search
    // never produced (a closure once made the listed fastest route faster).
    // Second in line, so the best route for the user's priorities stays first.
    Weights timeOnly{};
    timeOnly.per_minute = 1.0;
    std::vector<int> fastestPath;
    std::vector<double> timeCosts = costsFor(timeOnly);
    if (!timeCosts.empty()) {
        const DijkstraRouter fastestFinder(graph, std::move(timeCosts), blockedMask);
        std::vector<std::vector<int>> fastest = pathsFrom(fastestFinder);
        if (!fastest.empty()) fastestPath = std::move(fastest.front());
    }
    if (!fastestPath.empty() &&
        (generatedPaths.empty() || generatedPaths.front() != fastestPath)) {
        // Move it up even when the penalty search found it lower down, where
        // the overlap filter or the three-route limit could drop it.
        generatedPaths.erase(
            std::remove(generatedPaths.begin(), generatedPaths.end(), fastestPath),
            generatedPaths.end());
        generatedPaths.insert(
            generatedPaths.begin() + std::min<std::ptrdiff_t>(1, generatedPaths.size()),
            fastestPath);
    }

    std::vector<Candidate> candidates;

    for (const auto& path : generatedPaths) {
        if (path.size() < 2) continue;

        Candidate candidate;
        candidate.path = path;
        RouteInfo& route = candidate.route;
        route.name = "Alternative route";
        double durationSeconds = 0.0;
        double freeFlowSeconds = 0.0;
        double trafficWeightedSeconds = 0.0;
        double totalDistanceM = 0.0;
        double totalTrafficWeight = 0.0;
        double speedWeightedM = 0.0;
        double dataM = 0.0;        // metres of the route that have measured traffic
        double dataRatioM = 0.0;   // same, weighted by current / free-flow speed
        std::vector<int> routeEdges;

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

            routeEdges.push_back(edgeIndex);
            const Edge& edge = graph->edges[edgeIndex];
            // Closures and the traffic scenario both scale the travel time;
            // the "no traffic" ETA leaves out the traffic scenario only.
            const double edgeSeconds = std::max(0.0, edge.duration_s) *
                                       effect.durationFactor[edgeIndex];
            freeFlowSeconds += std::max(0.0, edge.duration_s) *
                               closureFactor[edgeIndex];

            totalDistanceM += std::max(0.0, edge.distance_m);
            durationSeconds += edgeSeconds;
            speedWeightedM += speed[edgeIndex] * std::max(0.0, edge.distance_m);

            double measuredIndex = 0.0;
            if (useSnapshot && snapshotHasData_[edgeIndex]) {
                measuredIndex = snapshotIndex_[edgeIndex];
                dataM += std::max(0.0, edge.distance_m);
                dataRatioM += (1.0 - measuredIndex) * std::max(0.0, edge.distance_m);
            }
            const double traffic = clampTraffic(
                edge.traffic_index + (1.0 - speed[edgeIndex]) + measuredIndex);
            trafficWeightedSeconds += traffic * edgeSeconds;
            totalTrafficWeight += edgeSeconds;

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
        // Documented major junctions the route arrives at, each counted once
        // (the traffic module's rule), with a wait at each.
        if (junctions_) {
            route.junctions = static_cast<int>(
                junctionsOnRoute(network_, *junctions_, routeEdges).size());
        }
        const double waitSeconds =
            static_cast<double>(route.junctions) * kJunctionWaitS;
        candidate.rawEta = durationSeconds + waitSeconds;

        route.distanceKm = candidate.rawDistance;
        route.freeFlowEtaSeconds = freeFlowSeconds + waitSeconds;
        route.etaSeconds = candidate.rawEta;

        // Data labels. A saved snapshot is not live traffic, so livePct stays 0;
        // the share of the route covered by snapshot data is reported as
        // "typical" (the time of the snapshot is shown by /api/traffic/status),
        // and the rest is an estimate from free-flow speeds. The level uses the
        // traffic module's thresholds on the length-weighted speed ratio over the
        // edges that have data. A traffic scenario is an estimate too.
        route.livePct = 0.0;
        route.typicalPct = 0.0;
        route.estimatedPct = 100.0;
        route.trafficLevel = "Estimated";
        if (useSnapshot && dataM > 0.0) {
            const double covered = 100.0 * dataM / totalDistanceM;
            route.typicalPct = covered;
            route.estimatedPct = 100.0 - covered;
            route.trafficLevel =
                levelName(levelForRatio(dataRatioM / dataM, TrafficRules{}));
        }
        if (trafficScenario == ScenarioKind::Rain ||
            trafficScenario == ScenarioKind::RushHour) {
            const TrafficLevel level =
                levelForRatio(speedWeightedM / totalDistanceM, TrafficRules{});
            route.trafficLevel =
                std::string(levelName(level)) +
                (trafficScenario == ScenarioKind::Rain ? " (rain estimate)"
                                                   : " (rush hour estimate)");
        }

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
        // The fastest route is never a detour, however long it is.
        if (candidate.path != fastestPath &&
            candidate.rawDistance >
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

            // A route mostly on the same roads still counts as different when
            // it is the fastest and clearly faster: users should see it.
            const bool clearlyFaster =
                candidate.path == fastestPath &&
                candidate.rawEta < accepted.rawEta - kDistinctEtaS;
            if (overlap >= maxSharedOverlap && !clearlyFaster) {
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

    // Rank by score with the C merge sort (src/core/sort.c). It copies elements
    // byte by byte, so it sorts plain (score, position) pairs rather than the
    // candidates themselves (which hold strings and vectors). Merge sort is
    // stable: routes with equal scores keep their order, so the penalty
    // method's cheapest route stays ahead of an equally scored alternative.
    std::vector<RankItem> ranking(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        ranking[i] = RankItem{ candidates[i].route.score, static_cast<int>(i) };
    }
    if (merge_sort(ranking.data(), ranking.size(), sizeof(RankItem), compareRankItems) != 0) {
        // Only out of memory; keep the order the routes were generated in.
        for (std::size_t i = 0; i < ranking.size(); ++i) {
            ranking[i].position = static_cast<int>(i);
        }
    }

    for (std::size_t i = 0; i < ranking.size(); ++i) {
        Candidate& candidate = candidates[ranking[i].position];
        candidate.route.name = "Route " + std::to_string(i + 1);
        candidate.route.recommended = (i == 0);
        result.push_back(std::move(candidate.route));
    }

    return result;
}

}  // namespace rl
