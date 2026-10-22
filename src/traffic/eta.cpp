#include "eta.hpp"

#include <cmath>
#include <cstdio>
#include <set>

namespace rl {

RouteSummary summarizeRoute(const OsmNetwork& net, const TrafficState& state, const std::vector<int>& routeEdges,
                            const TrafficRules& rules, double signalDelayS,
                            const JunctionIndex* junctions) {
    RouteSummary s;
    std::vector<char> seenJ(junctions ? junctions->junctions.size() : 0, 0);
    std::set<long long> signals;
    double live = 0, hist = 0, est = 0, ratioWeighted = 0;
    for (int e : routeEdges) {
        if (e < 0 || e >= net.graph->edge_count) continue;
        const Edge& ge = net.graph->edges[e];
        const EdgeTraffic& t = state.edge(e);
        for (int i = 0; i < ge.signal_count; i++) signals.insert(ge.signal_ids[i]);

        double signalWait;
        if (junctions) {   // documented major junction at the end of this edge, counted once per route
            int j = junctions->junctionOf(ge.to);
            bool first = j >= 0 && !seenJ[j];
            signalWait = first ? signalDelayS : 0.0;
            if (first) { seenJ[j] = 1; s.junctionNames.push_back(junctions->junctions[j].name); }
        } else {
            signalWait = signalDelayS * ge.signal_count;
        }
        s.distanceM += ge.distance_m;
        s.freeFlowS += ge.duration_s + signalWait;
        if (t.source == DataSource::FreeFlow) {
            s.etaS += ge.duration_s + signalWait;
            est += ge.distance_m;
        } else {
            s.etaS += t.durationS;   // TomTom speed already includes signal waits
            (t.source == DataSource::Live ? live : hist) += ge.distance_m;
            ratioWeighted += t.ratio * ge.distance_m;
            if (t.closed) s.anyClosure = true;
        }
    }
    s.signals = (int)signals.size();
    s.junctions = (int)s.junctionNames.size();
    if (s.distanceM > 0) {
        s.liveShare = live / s.distanceM;
        s.historicalShare = hist / s.distanceM;
        s.estimatedShare = est / s.distanceM;
    }
    if (live + hist > 0) {
        s.meanRatio = ratioWeighted / (live + hist);
        s.level = levelForRatio(s.meanRatio, rules);
    }
    return s;
}

std::string formatDuration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) return "unknown";
    long minutes = std::lround(seconds / 60.0);
    if (minutes < 1) return "< 1 min";
    if (minutes < 60) return std::to_string(minutes) + " min";
    long h = minutes / 60, m = minutes % 60;
    return std::to_string(h) + " h" + (m ? " " + std::to_string(m) + " min" : "");
}

std::string formatDistance(double meters) {
    char buf[32];
    if (meters < 1000) std::snprintf(buf, sizeof buf, "%.0f m", meters);
    else std::snprintf(buf, sizeof buf, "%.1f km", meters / 1000.0);
    return buf;
}

std::string coverageLabel(const RouteSummary& s) {
    auto pct = [](double f) { return std::to_string((int)std::lround(f * 100)) + "%"; };
    if (s.liveShare + s.historicalShare <= 0) return "no traffic data on this route: ETA is an estimate";
    return "live on " + pct(s.liveShare) + " of the route, typical on " + pct(s.historicalShare) +
           ", estimated on " + pct(s.estimatedShare);
}

}  // namespace rl
