#include "traffic_provider.hpp"

#include <algorithm>

namespace rl {

// ---------------------------------------------------------------- SnapshotProvider

SnapshotProvider::SnapshotProvider(const OsmNetwork& net, const std::string& csvPath) : net_(net) {
    if (ts_load(&snapshot_, csvPath.c_str()) != 0) {
        ts_free(&snapshot_);
        throw LoadError("cannot read traffic snapshot '" + csvPath + "'");
    }
    for (int e = 0; e < (int)net_.csvRow.size(); e++)
        if (recordFor(e) != nullptr) edgesWithData_++;
}

SnapshotProvider::~SnapshotProvider() { ts_free(&snapshot_); }

const TrafficRecord* SnapshotProvider::recordFor(int edge) const {
    if (edge < 0 || edge >= (int)net_.csvRow.size()) return nullptr;
    const TrafficRecord* r = nullptr;
    return ts_find(&snapshot_, net_.csvRow[edge], &r) ? r : nullptr;
}

double SnapshotProvider::trafficIndex(int edge) const {
    const TrafficRecord* r = recordFor(edge);
    return r ? r->traffic_index : 0.0;
}

double SnapshotProvider::currentKph(int edge) const {
    const TrafficRecord* r = recordFor(edge);
    return r ? r->current_kph : 0.0;
}

double SnapshotProvider::confidence(int edge) const {
    const TrafficRecord* r = recordFor(edge);
    return r ? r->confidence : 0.0;
}

std::string SnapshotProvider::description() const {
    std::string when = snapshot_.fetched_at[0] ? snapshot_.fetched_at : "time unknown";
    return "snapshot " + when;
}

// ---------------------------------------------------------------- factory and adapter

std::unique_ptr<TrafficProvider> makeTrafficProvider(const OsmNetwork& net, const std::string& csvPath,
                                                     std::string* warning) {
    try {
        return std::make_unique<SnapshotProvider>(net, csvPath);
    } catch (const LoadError& e) {
        if (warning) *warning = std::string(e.what()) + "; using free-flow speeds (estimated)";
        return std::make_unique<FreeFlowProvider>();
    }
}

int applyProvider(TrafficState& state, const TrafficProvider& provider) {
    OsmNetwork& net = state.network();
    int updated = 0;
    for (int e = 0; e < state.edgeCount(); e++) {
        if (!provider.hasData(e)) continue;
        Edge& ge = net.graph->edges[e];
        EdgeTraffic& t = state.mutableEdge(e);
        double index = std::clamp(provider.trafficIndex(e), 0.0, 1.0);
        double kph = provider.currentKph(e);
        bool closed = kph <= 0.0;
        t.source = DataSource::Historical;
        t.ratio = 1.0 - index;
        t.confidence = provider.confidence(e);
        t.closed = closed;
        t.durationS = closed ? ge.duration_s : ge.distance_m / (kph / 3.6);
        ge.traffic_index = closed ? 1.0 : index;
        updated++;
    }
    return updated;
}

}  // namespace rl
