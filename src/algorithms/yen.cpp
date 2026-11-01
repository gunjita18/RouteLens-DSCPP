
#include "yen.hpp"
#include "dijkstra.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace rl {
namespace {

// Compare paths by their vertex sequence.
struct Candidate {
    std::vector<int> path;
    double cost;
};

bool candidateLess(const Candidate& a, const Candidate& b) {
    if (a.cost != b.cost) {
        return a.cost < b.cost;
    }
    return a.path < b.path;
}

// Calculate the cost of a path using the graph's actual edge costs.
// If parallel edges exist, use the cheapest matching directed edge.
double pathCost(
    const Graph* graph,
    const std::vector<double>& edgeCosts,
    const std::vector<int>& path
) {
    double total = 0.0;

    for (std::size_t i = 1; i < path.size(); ++i) {
        int from = path[i - 1];
        int to = path[i];

        double cheapest = std::numeric_limits<double>::infinity();

        for (int e = graph->first_out[from];
             e != -1;
             e = graph->next_out[e]) {
            if (e < 0 || e >= graph->edge_count) {
                return std::numeric_limits<double>::infinity();
            }

            if (graph->edges[e].to == to &&
                std::isfinite(edgeCosts[e]) &&
                edgeCosts[e] >= 0.0) {
                cheapest = std::min(cheapest, edgeCosts[e]);
            }
        }

        if (!std::isfinite(cheapest)) {
            return std::numeric_limits<double>::infinity();
        }

        total += cheapest;
    }

    return total;
}

} // namespace

YenResult yenKShortestPaths(
    const Graph* graph,
    const Weights* weights,
    int source,
    int destination,
    int k
) {
    YenResult result;

    if (!graph || !weights || graph->node_count <= 0 ||
        graph->edge_count < 0 || k <= 0 ||
        source < 0 || destination < 0 ||
        source >= graph->node_count ||
        destination >= graph->node_count ||
        !weights_valid(weights)) {
        return result;
    }

    const int n = graph->node_count;
    const int m = graph->edge_count;

    if (m > 0 && (!graph->edges || !graph->first_out ||
                  !graph->next_out)) {
        return result;
    }

    // Compute the same weighted edge costs used by Dijkstra.
    std::vector<double> edgeCosts(m);

    if (m > 0 &&
        !graph_compute_costs(graph, weights, edgeCosts.data())) {
        return result;
    }

    // The only route from a vertex to itself is the zero-edge path.
    if (source == destination) {
        result.paths.push_back({source});
        result.total_costs.push_back(0.0);
        return result;
    }

    // First shortest route.
    DijkstraResult first = dijkstra(
        graph, weights, source, destination
    );

    if (!first.found || first.path.empty()) {
        return result;
    }

    result.paths.push_back(first.path);
    result.total_costs.push_back(first.total_cost);

    // Accepted paths and pending candidate paths.
    std::set<std::vector<int>> acceptedPaths;
    std::set<std::vector<int>> candidatePaths;

    acceptedPaths.insert(first.path);

    std::vector<Candidate> candidates;

    // Generate alternatives until k paths are accepted or none remain.
    while (static_cast<int>(result.paths.size()) < k) {
        const std::vector<int>& previousPath = result.paths.back();

        // Each spur index defines a root prefix and a spur search.
        for (std::size_t spurIndex = 0;
             spurIndex + 1 < previousPath.size();
             ++spurIndex) {

            std::vector<int> rootPath(
                previousPath.begin(),
                previousPath.begin() + spurIndex + 1
            );

            int spurNode = rootPath.back();

            // Block the next edge of every accepted route that has
            // the same root prefix.
            std::vector<bool> blockedEdges(m, false);

            for (const auto& accepted : result.paths) {
                if (accepted.size() <= spurIndex + 1) {
                    continue;
                }

                bool sameRoot = true;

                for (std::size_t j = 0; j <= spurIndex; ++j) {
                    if (accepted[j] != rootPath[j]) {
                        sameRoot = false;
                        break;
                    }
                }

                if (!sameRoot) {
                    continue;
                }

                int from = accepted[spurIndex];
                int to = accepted[spurIndex + 1];

                for (int e = graph->first_out[from];
                     e != -1;
                     e = graph->next_out[e]) {
                    if (e < 0 || e >= m) {
                        continue;
                    }

                    if (graph->edges[e].to == to) {
                        blockedEdges[e] = true;
                    }
                }
            }

            // Block earlier root vertices so the resulting path
            // cannot loop back into its own prefix.
            std::vector<bool> blockedNodes(n, false);

            for (std::size_t j = 0; j + 1 < rootPath.size(); ++j) {
                blockedNodes[rootPath[j]] = true;
            }

            // Dijkstra's blocked-edge support handles road removal.
            // To block root vertices, construct a temporary graph
            // with their outgoing edges omitted.
            Graph temporary = *graph;
            std::vector<int> firstOut(n);
            std::vector<int> nextOut(m);

            for (int u = 0; u < n; ++u) {
                firstOut[u] = -1;
            }

            // Rebuild forward-star links while omitting blocked nodes
            // and blocked edges. Edge records and their costs stay intact.
            for (int e = 0; e < m; ++e) {
                nextOut[e] = -1;
            }

            for (int e = m - 1; e >= 0; --e) {
                int from = graph->edges[e].from;
                int to = graph->edges[e].to;

                if (from < 0 || from >= n ||
                    to < 0 || to >= n ||
                    blockedNodes[from] ||
                    blockedEdges[e]) {
                    continue;
                }

                nextOut[e] = firstOut[from];
                firstOut[from] = e;
            }

            temporary.first_out = firstOut.data();
            temporary.next_out = nextOut.data();

            DijkstraResult spur = dijkstra(
                &temporary,
                weights,
                spurNode,
                destination
            );

            if (!spur.found || spur.path.empty()) {
                continue;
            }

            // Combine root prefix and spur path, without repeating
            // the spur node.
            std::vector<int> totalPath = rootPath;

            totalPath.insert(
                totalPath.end(),
                spur.path.begin() + 1,
                spur.path.end()
            );

            if (acceptedPaths.count(totalPath) ||
                candidatePaths.count(totalPath)) {
                continue;
            }

            double totalCost = pathCost(
                graph, edgeCosts, totalPath
            );

            if (!std::isfinite(totalCost)) {
                continue;
            }

            candidates.push_back({totalPath, totalCost});
            candidatePaths.insert(totalPath);
        }

        if (candidates.empty()) {
            break;
        }

        // Choose the lowest-cost candidate, breaking ties by path.
        auto best = std::min_element(
            candidates.begin(),
            candidates.end(),
            candidateLess
        );

        Candidate chosen = *best;
        candidates.erase(best);
        candidatePaths.erase(chosen.path);

        acceptedPaths.insert(chosen.path);
        result.paths.push_back(chosen.path);
        result.total_costs.push_back(chosen.cost);
    }

    return result;
}

} // namespace rl
