// routelens_eval — algorithm evaluation for Phase III (the C routing core).
//
// Runs the hand-written C routing core, through DijkstraRouter and PenaltyRouter
// (src/oop/route_finder.hpp), on the real Dehradun road network and writes CSV
// files that evaluation/compare_networkx.py checks against NetworkX and summarises:
//
//   eval_graph.csv    the loaded graph in index space (for the NetworkX check)
//   profiles.csv      the weight profiles used
//   pairs.csv         per random origin-destination pair and profile: the cost the
//                     C Dijkstra found, its run time and the route length in edges
//   alternatives.csv  per pair: routes from the penalty method, how many survive the
//                     server's filter, overlap, stretch, run time
//
// Usage: routelens_eval <data_dir> <out_dir> [--pairs N] [--alt-pairs M] [--seed S]
// Build with -DCMAKE_BUILD_TYPE=Release; timings from an unoptimised build are
// not meaningful (evaluation/run_evaluation.sh does this).
//
// The Phase II version (A*, Yen's algorithm, std::priority_queue comparison) is in
// Git history together with its report, docs/evaluation_phase2.md.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "../data/osm_loader.hpp"
#include "../oop/route.hpp"
#include "../oop/route_finder.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Profile {
    const char* name;
    Weights weights;
};

// Same weights across the C run and the NetworkX check (written to profiles.csv).
const Profile kProfiles[] = {
    {"time", {0.0, 1.0, 0.0, 0.0}},
    {"distance", {1.0, 0.0, 0.0, 0.0}},
    {"balanced", {0.4, 0.4, 0.1, 0.1}},
};

// Penalty settings the server uses (src/server/osm_route_provider.cpp).
constexpr int kAltRoutes = 5;
constexpr double kMaxStretch = 1.5;
// The server's later filter: a route is "usable" if its stretch is at most 1.5
// and it shares less than 85 % of its length with every route kept before it.
constexpr double kUsableStretch = 1.5;
constexpr double kUsableOverlap = 0.85;

double microsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

// Road segments of a route with their lengths, keyed by the unordered node pair
// so a road shared in opposite directions still counts as shared.
using SegmentMap = std::map<std::pair<int, int>, double>;

SegmentMap segmentsOf(const Graph* g, const rl::Route& route) {
    SegmentMap segments;
    for (int e : route.edges()) {
        const Edge& ed = g->edges[e];
        segments[{std::min(ed.from, ed.to), std::max(ed.from, ed.to)}] = ed.distance_m;
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
    const double shorter = std::min(totalLength(a), totalLength(b));
    if (shorter <= 0.0) return 0.0;
    double shared = 0.0;
    for (const auto& kv : a) {
        if (b.count(kv.first)) shared += kv.second;
    }
    return shared / shorter;
}

struct AltStats {
    int routes = 0;
    int usable = 0;
    double meanOverlap = 0.0;
    double maxOverlap = 0.0;
    double meanStretch = 0.0;
    double maxStretch = 0.0;
};

AltStats analyse(const Graph* g, const std::vector<rl::Route>& routes) {
    AltStats s;
    s.routes = static_cast<int>(routes.size());
    if (routes.empty()) return s;

    std::vector<SegmentMap> segs;
    for (const auto& r : routes) segs.push_back(segmentsOf(g, r));
    const double cheapest = routes.front().cost();   // PenaltyRouter: first is cheapest

    double stretchSum = 0.0;
    for (const auto& r : routes) {
        const double stretch = cheapest > 0.0 ? r.cost() / cheapest : 1.0;
        stretchSum += stretch;
        s.maxStretch = std::max(s.maxStretch, stretch);
    }
    s.meanStretch = stretchSum / routes.size();

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
    for (std::size_t i = 0; i < routes.size(); ++i) {
        const double stretch = cheapest > 0.0 ? routes[i].cost() / cheapest : 1.0;
        if (stretch > kUsableStretch) continue;
        bool similar = false;
        for (std::size_t k : kept) {
            if (overlap(segs[i], segs[k]) >= kUsableOverlap) {
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

std::vector<double> costsFor(const Graph* g, const Weights& w) {
    std::vector<double> costs(g->edge_count);
    graph_compute_costs(g, &w, costs.data());
    return costs;
}

}  // namespace

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

    // ---- C Dijkstra (through DijkstraRouter) on every pair and profile ----
    {
        std::ofstream out(outDir + "/pairs.csv");
        out.precision(17);
        out << "pair,src,dst,profile,found,cost,time_us,route_edges\n";

        for (const Profile& profile : kProfiles) {
            const rl::DijkstraRouter router(g, costsFor(g, profile.weights));
            const rl::RouteFinder& finder = router;

            // Warm caches so the first pairs are not penalised.
            for (int w = 0; w < 10 && w < pairCount; ++w) finder.find(pairs[w].first, pairs[w].second);

            for (int i = 0; i < pairCount; ++i) {
                const int s = pairs[i].first;
                const int t = pairs[i].second;
                const auto t0 = Clock::now();
                const std::vector<rl::Route> routes = finder.find(s, t);
                const double us = microsSince(t0);
                const bool found = !routes.empty();
                out << i << ',' << s << ',' << t << ',' << profile.name << ',' << (found ? 1 : 0)
                    << ',' << (found ? routes[0].cost() : 0.0) << ',' << us << ','
                    << (found ? routes[0].edgeCount() : 0) << '\n';
            }
            std::cout << "profile " << profile.name << ": " << pairCount << " pairs done\n";
        }
    }

    // ---- penalty method (through PenaltyRouter), balanced profile ----
    {
        std::ofstream out(outDir + "/alternatives.csv");
        out.precision(17);
        out << "pair,src,dst,method,routes,usable,time_ms,mean_overlap,max_overlap,"
               "mean_stretch,max_stretch\n";

        ::PenaltyOptions options = penalty_default_options();
        options.k = kAltRoutes;
        options.max_stretch = kMaxStretch;
        const rl::PenaltyRouter router(g, costsFor(g, kProfiles[2].weights), {}, options);

        for (int i = 0; i < altPairCount; ++i) {
            const int s = pairs[i].first;
            const int t = pairs[i].second;
            const auto t0 = Clock::now();
            const std::vector<rl::Route> routes = router.find(s, t);
            const double ms = microsSince(t0) / 1000.0;
            const AltStats st = analyse(g, routes);
            out << i << ',' << s << ',' << t << ",penalty," << st.routes << ',' << st.usable << ','
                << ms << ',' << st.meanOverlap << ',' << st.maxOverlap << ',' << st.meanStretch
                << ',' << st.maxStretch << '\n';
            if ((i + 1) % 10 == 0) {
                std::cout << "alternatives: " << (i + 1) << "/" << altPairCount << " pairs\n";
            }
        }
    }

    std::ofstream meta(outDir + "/run_meta.txt");
    meta << "seed=" << seed << "\npairs=" << pairCount << "\nalt_pairs=" << altPairCount
         << "\nnodes=" << g->node_count << "\nedges=" << g->edge_count
         << "\nalt_routes=" << kAltRoutes << "\npenalty_factor=" << penalty_default_options().factor
         << "\nmax_overlap=" << penalty_default_options().max_overlap
         << "\nmax_stretch=" << kMaxStretch << "\n";

    std::cout << "wrote results to " << outDir << "\n";
    return 0;
}
