// routelens_eval — algorithm evaluation for Phase II.
//
// Runs on the real Dehradun road network and writes CSV files that
// evaluation/compare_networkx.py checks against NetworkX and summarises:
//
//   eval_graph.csv    the loaded graph in index space (for the NetworkX check)
//   profiles.csv      the weight profiles used
//   pairs.csv         per random origin-destination pair and profile: cost, time
//                     and vertices expanded for Dijkstra (own heap), A*, and a
//                     Dijkstra using std::priority_queue
//   alternatives.csv  per pair and generator (penalty method, Yen): number of
//                     routes, overlap, stretch, usable alternatives, run time
//
// Usage: routelens_eval <data_dir> <out_dir> [--pairs N] [--alt-pairs M] [--seed S]
// Build with -DCMAKE_BUILD_TYPE=Release; timings from an unoptimised build are
// not meaningful (evaluation/run_evaluation.sh does this).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "../algorithms/astar.hpp"
#include "../algorithms/dijkstra.hpp"
#include "../algorithms/penalty.hpp"
#include "../algorithms/yen.hpp"
#include "../data/osm_loader.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Profile {
    const char* name;
    Weights weights;
};

// Same weights across the C++ run and the NetworkX check (written to profiles.csv).
const Profile kProfiles[] = {
    {"time", {0.0, 1.0, 0.0, 0.0}},
    {"distance", {1.0, 0.0, 0.0, 0.0}},
    {"balanced", {0.4, 0.4, 0.1, 0.1}},
};

double microsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

// Reference implementation: textbook Dijkstra on std::priority_queue with
// lazy deletion and the same early exit as rl::dijkstraWithCosts. It records
// parents and rebuilds the vertex and edge paths exactly as the RouteLens
// search does, so the only difference measured is the priority queue.
struct PqResult {
    bool found = false;
    double cost = 0.0;
    std::size_t expanded = 0;
    std::vector<int> path;
    std::vector<int> edgePath;
};

PqResult dijkstraStdPq(const Graph* g, const std::vector<double>& costs, int s, int t) {
    PqResult r;
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> dist(g->node_count, inf);
    std::vector<int> parent(g->node_count, -1);
    std::vector<int> parentEdge(g->node_count, -1);
    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;

    dist[s] = 0.0;
    pq.push({0.0, s});

    while (!pq.empty()) {
        const Item top = pq.top();
        pq.pop();
        const int u = top.second;
        if (top.first > dist[u]) continue;
        ++r.expanded;
        if (u == t) break;

        for (int e = g->first_out[u]; e != -1; e = g->next_out[e]) {
            const int v = g->edges[e].to;
            const double nd = dist[u] + costs[e];
            if (nd < dist[v]) {
                dist[v] = nd;
                parent[v] = u;
                parentEdge[v] = e;
                pq.push({nd, v});
            }
        }
    }

    r.found = std::isfinite(dist[t]);
    if (!r.found) return r;

    r.cost = dist[t];
    for (int v = t; v != s; v = parent[v]) {
        r.path.push_back(v);
        r.edgePath.push_back(parentEdge[v]);
    }
    r.path.push_back(s);
    std::reverse(r.path.begin(), r.path.end());
    std::reverse(r.edgePath.begin(), r.edgePath.end());
    return r;
}

// Road segments of a vertex path with their lengths, keyed by the unordered
// vertex pair so a road shared in opposite directions still counts as shared.
using SegmentMap = std::map<std::pair<int, int>, double>;

SegmentMap segmentsOf(const Graph* g, const std::vector<int>& path) {
    SegmentMap segments;
    for (std::size_t i = 1; i < path.size(); ++i) {
        const int a = path[i - 1];
        const int b = path[i];
        double best = std::numeric_limits<double>::infinity();
        for (int e = g->first_out[a]; e != -1; e = g->next_out[e]) {
            if (g->edges[e].to == b) best = std::min(best, g->edges[e].distance_m);
        }
        if (std::isfinite(best)) {
            segments[{std::min(a, b), std::max(a, b)}] = best;
        }
    }
    return segments;
}

double totalLength(const SegmentMap& m) {
    double sum = 0.0;
    for (const auto& kv : m) sum += kv.second;
    return sum;
}

// Share of the shorter route's length that the other route also uses.
double overlap(const SegmentMap& a, const SegmentMap& b) {
    const double la = totalLength(a);
    const double lb = totalLength(b);
    const double shorter = std::min(la, lb);
    if (shorter <= 0.0) return 0.0;
    double shared = 0.0;
    for (const auto& kv : a) {
        if (b.count(kv.first)) shared += kv.second;
    }
    return shared / shorter;
}

double pathCost(const Graph* g, const std::vector<double>& costs, const std::vector<int>& path) {
    double sum = 0.0;
    for (std::size_t i = 1; i < path.size(); ++i) {
        double best = std::numeric_limits<double>::infinity();
        for (int e = g->first_out[path[i - 1]]; e != -1; e = g->next_out[e]) {
            if (g->edges[e].to == path[i]) best = std::min(best, costs[e]);
        }
        sum += best;
    }
    return sum;
}

struct AltStats {
    int routes = 0;
    int usable = 0;  // after the overlap and stretch filter RouteLens applies
    double meanOverlap = 0.0;
    double maxOverlap = 0.0;
    double meanStretch = 0.0;
    double maxStretch = 0.0;
};

// Overlap is measured between every pair of returned routes; "usable" counts
// routes that would survive the provider's filter (stretch <= 1.5 of the
// shortest, overlap < 0.85 with every route kept so far).
AltStats analyse(const Graph* g, const std::vector<double>& costs,
                 const std::vector<std::vector<int>>& paths) {
    AltStats s;
    s.routes = static_cast<int>(paths.size());
    if (paths.empty()) return s;

    std::vector<SegmentMap> segs;
    std::vector<double> pc;
    for (const auto& p : paths) {
        segs.push_back(segmentsOf(g, p));
        pc.push_back(pathCost(g, costs, p));
    }

    const double shortest = *std::min_element(pc.begin(), pc.end());

    double stretchSum = 0.0;
    for (double c : pc) {
        const double stretch = shortest > 0.0 ? c / shortest : 1.0;
        stretchSum += stretch;
        s.maxStretch = std::max(s.maxStretch, stretch);
    }
    s.meanStretch = stretchSum / pc.size();

    double overlapSum = 0.0;
    int pairs = 0;
    for (std::size_t i = 0; i < segs.size(); ++i) {
        for (std::size_t j = i + 1; j < segs.size(); ++j) {
            const double o = overlap(segs[i], segs[j]);
            overlapSum += o;
            s.maxOverlap = std::max(s.maxOverlap, o);
            ++pairs;
        }
    }
    s.meanOverlap = pairs > 0 ? overlapSum / pairs : 0.0;

    std::vector<std::size_t> kept;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const double stretch = shortest > 0.0 ? pc[i] / shortest : 1.0;
        if (stretch > 1.5) continue;
        bool similar = false;
        for (std::size_t k : kept) {
            if (overlap(segs[i], segs[k]) >= 0.85) {
                similar = true;
                break;
            }
        }
        if (!similar) kept.push_back(i);
    }
    s.usable = static_cast<int>(kept.size());
    return s;
}

bool parseInt(const char* text, int& out) {
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || v < 0 || v > 100000000) return false;
    out = static_cast<int>(v);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: routelens_eval <data_dir> <out_dir> "
                     "[--pairs N] [--alt-pairs M] [--seed S]\n";
        return 2;
    }

    const std::string dataDir = argv[1];
    const std::string outDir = argv[2];
    int pairCount = 1000;
    int altPairCount = 100;
    int seed = 42;

    for (int i = 3; i < argc; i += 2) {
        const std::string flag = argv[i];
        int* target = flag == "--pairs"       ? &pairCount
                      : flag == "--alt-pairs" ? &altPairCount
                      : flag == "--seed"      ? &seed
                                              : nullptr;
        if (!target || i + 1 >= argc || !parseInt(argv[i + 1], *target)) {
            std::cerr << "bad argument: " << flag << "\n";
            return 2;
        }
    }
    altPairCount = std::min(altPairCount, pairCount);

    rl::OsmNetwork net;
    std::string err;
    if (!rl::loadOsmNetwork(dataDir, net, err)) {
        std::cerr << "cannot load network from " << dataDir << ": " << err << "\n";
        return 1;
    }
    const Graph* g = net.graph;
    std::cout << "network: " << g->node_count << " nodes, " << g->edge_count << " edges\n";

    // ---- graph and profiles, for the NetworkX reference ----
    {
        std::ofstream out(outDir + "/eval_graph.csv");
        if (!out) {
            std::cerr << "cannot write to " << outDir << " (create it first)\n";
            return 1;
        }
        out.precision(17);
        out << "from,to,distance_m,duration_s,traffic_index,signal_count\n";
        for (int e = 0; e < g->edge_count; ++e) {
            const Edge& ed = g->edges[e];
            out << ed.from << ',' << ed.to << ',' << ed.distance_m << ',' << ed.duration_s << ','
                << ed.traffic_index << ',' << ed.signal_count << '\n';
        }

        std::ofstream prof(outDir + "/profiles.csv");
        prof.precision(17);
        prof << "name,per_km,per_minute,per_congested_minute,per_signal\n";
        for (const Profile& p : kProfiles) {
            prof << p.name << ',' << p.weights.per_km << ',' << p.weights.per_minute << ','
                 << p.weights.per_congested_minute << ',' << p.weights.per_signal << '\n';
        }
    }

    // ---- random origin-destination pairs (all nodes share one SCC) ----
    std::mt19937 rng(static_cast<unsigned>(seed));
    std::uniform_int_distribution<int> pick(0, g->node_count - 1);
    std::vector<std::pair<int, int>> pairs;
    while (static_cast<int>(pairs.size()) < pairCount) {
        const int s = pick(rng);
        const int t = pick(rng);
        if (s != t) pairs.push_back({s, t});
    }

    const double maxSpeed = rl::graphMaxSpeedMps(g);

    // ---- Dijkstra (own heap) vs A* vs Dijkstra (std::priority_queue) ----
    {
        std::ofstream out(outDir + "/pairs.csv");
        out.precision(17);
        out << "pair,src,dst,profile,found,dijkstra_cost,astar_cost,pq_cost,"
               "dijkstra_us,astar_us,pq_us,dijkstra_expanded,astar_expanded,pq_expanded,"
               "path_nodes\n";

        for (const Profile& profile : kProfiles) {
            std::vector<double> costs(g->edge_count);
            graph_compute_costs(g, &profile.weights, costs.data());

            // Warm caches so the first pairs are not penalised.
            for (int w = 0; w < 10 && w < pairCount; ++w) {
                rl::dijkstraWithCosts(g, costs, pairs[w].first, pairs[w].second);
                dijkstraStdPq(g, costs, pairs[w].first, pairs[w].second);
            }

            for (int i = 0; i < pairCount; ++i) {
                const int s = pairs[i].first;
                const int t = pairs[i].second;

                rl::DijkstraResult d;
                rl::AStarResult a;
                PqResult q;
                double dUs = 0, aUs = 0, qUs = 0;

                // Rotate the run order so no algorithm always goes first.
                for (int turn = 0; turn < 3; ++turn) {
                    switch ((i + turn) % 3) {
                        case 0: {
                            const auto t0 = Clock::now();
                            d = rl::dijkstraWithCosts(g, costs, s, t);
                            dUs = microsSince(t0);
                            break;
                        }
                        case 1: {
                            const auto t0 = Clock::now();
                            a = rl::astarWithCosts(g, costs, &profile.weights, &net.pos, s, t,
                                                   maxSpeed);
                            aUs = microsSince(t0);
                            break;
                        }
                        default: {
                            const auto t0 = Clock::now();
                            q = dijkstraStdPq(g, costs, s, t);
                            qUs = microsSince(t0);
                            break;
                        }
                    }
                }

                out << i << ',' << s << ',' << t << ',' << profile.name << ','
                    << (d.found && a.found && q.found ? 1 : 0) << ',' << d.total_cost << ','
                    << a.total_cost << ',' << q.cost << ',' << dUs << ',' << aUs << ',' << qUs
                    << ',' << d.nodes_expanded << ',' << a.nodes_expanded << ',' << q.expanded
                    << ',' << d.path.size() << '\n';
            }
            std::cout << "profile " << profile.name << ": " << pairCount << " pairs done\n";
        }
    }

    // ---- penalty method vs Yen's algorithm ----
    {
        std::ofstream out(outDir + "/alternatives.csv");
        out.precision(17);
        out << "pair,src,dst,method,routes,usable,time_ms,mean_overlap,max_overlap,"
               "mean_stretch,max_stretch\n";

        const Weights balanced = kProfiles[2].weights;
        std::vector<double> costs(g->edge_count);
        graph_compute_costs(g, &balanced, costs.data());

        constexpr int kRoutes = 5;

        for (int i = 0; i < altPairCount; ++i) {
            const int s = pairs[i].first;
            const int t = pairs[i].second;

            {
                rl::PenaltyOptions options;
                options.k = kRoutes;
                options.penaltyFactor = 1.5;
                const auto t0 = Clock::now();
                const rl::PenaltyResult r = rl::penaltyAlternatives(g, costs, s, t, options);
                const double ms = microsSince(t0) / 1000.0;
                const AltStats st = analyse(g, costs, r.paths);
                out << i << ',' << s << ',' << t << ",penalty," << st.routes << ',' << st.usable
                    << ',' << ms << ',' << st.meanOverlap << ',' << st.maxOverlap << ','
                    << st.meanStretch << ',' << st.maxStretch << '\n';
            }
            {
                const auto t0 = Clock::now();
                const rl::YenResult r = rl::yenKShortestPaths(g, &balanced, s, t, kRoutes);
                const double ms = microsSince(t0) / 1000.0;
                const AltStats st = analyse(g, costs, r.paths);
                out << i << ',' << s << ',' << t << ",yen," << st.routes << ',' << st.usable << ','
                    << ms << ',' << st.meanOverlap << ',' << st.maxOverlap << ','
                    << st.meanStretch << ',' << st.maxStretch << '\n';
            }

            if ((i + 1) % 10 == 0) {
                std::cout << "alternatives: " << (i + 1) << "/" << altPairCount << " pairs\n";
            }
        }
    }

    std::ofstream meta(outDir + "/run_meta.txt");
    meta << "seed=" << seed << "\npairs=" << pairCount << "\nalt_pairs=" << altPairCount
         << "\nnodes=" << g->node_count << "\nedges=" << g->edge_count
         << "\nmax_speed_kph=" << maxSpeed * 3.6 << "\n";

    std::cout << "wrote results to " << outDir << "\n";
    return 0;
}
