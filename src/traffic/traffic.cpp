#include "traffic.hpp"

#include <cmath>

namespace rl {

// ---------------------------------------------------------------- rules

TrafficLevel levelForRatio(double ratio, const TrafficRules& r) {
    if (!std::isfinite(ratio) || ratio < 0) return TrafficLevel::Unknown;
    if (ratio >= r.lowRatio) return TrafficLevel::Low;
    if (ratio >= r.highRatio) return TrafficLevel::Medium;
    return TrafficLevel::High;
}

const char* levelName(TrafficLevel l) {
    switch (l) {
        case TrafficLevel::Low: return "Low";
        case TrafficLevel::Medium: return "Medium";
        case TrafficLevel::High: return "High";
        default: return "Unknown";
    }
}

const char* sourceName(DataSource s) {
    switch (s) {
        case DataSource::Live: return "live";
        case DataSource::Historical: return "saved snapshot";
        case DataSource::Scenario: return "scenario estimate";
        default: return "estimated (free-flow)";
    }
}

// ---------------------------------------------------------------- TrafficState

TrafficState::TrafficState(OsmNetwork& net) : net_(net) { reset(); }

void TrafficState::reset() {
    edges_.assign(net_.graph ? net_.graph->edge_count : 0, EdgeTraffic{});
    for (size_t e = 0; e < edges_.size(); e++) {
        edges_[e].durationS = net_.graph->edges[e].duration_s;
        net_.graph->edges[e].traffic_index = 0.0;
    }
}

void TrafficState::computeCosts(const Weights& w, std::vector<double>& out) const {
    out.resize(edges_.size());
    for (size_t e = 0; e < edges_.size(); e++) {
        if (edges_[e].blocked) { out[e] = 1e9; continue; }   // blocked by an incident: avoid
        const Edge& ge = net_.graph->edges[e];
        double min = edges_[e].durationS / 60.0;
        out[e] = w.per_km * ge.distance_m / 1000.0 + w.per_minute * min +
                 w.per_congested_minute * ge.traffic_index * min + w.per_signal * ge.signal_count;
    }
}

}  // namespace rl
