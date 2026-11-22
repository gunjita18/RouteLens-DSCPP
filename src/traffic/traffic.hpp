// traffic.hpp — per-edge traffic on the OSM road graph (no network code here).
//
//   TrafficRules   the thresholds that turn a speed ratio into Low / Medium / High
//   EdgeTraffic    traffic of one edge: data source, speed ratio, travel time, flags
//   TrafficState   EdgeTraffic for every graph edge; free-flow until data is applied
//
// Where the data comes from is not decided here: a TrafficProvider
// (src/oop/traffic_provider.hpp) reads the saved TomTom snapshot, and
// applyProvider() writes it into a TrafficState.
#pragma once
#include <string>
#include <vector>

#include "../core/graph.h"
#include "../data/osm_loader.hpp"

namespace rl {

enum class TrafficLevel { Low, Medium, High, Unknown };

// Where an edge's speed comes from. A saved snapshot is Historical (it is not
// live). Live is kept so route summaries can still report it, but nothing sets it
// since the live TomTom client was removed. Scenario is a what-if estimate.
enum class DataSource { FreeFlow, Historical, Live, Scenario };

struct TrafficRules {
    double lowRatio = 0.8;             // current/free-flow >= 0.8 -> Low   (our design choice)
    double highRatio = 0.5;            // current/free-flow <  0.5 -> High  (our design choice)
};

TrafficLevel levelForRatio(double ratio, const TrafficRules& r);   // ratio = current / free-flow
const char* levelName(TrafficLevel level);
const char* sourceName(DataSource source);

struct EdgeTraffic {
    DataSource source = DataSource::FreeFlow;
    double ratio = 1.0;          // current / free-flow speed (1.0 when no data)
    double confidence = 0.0;
    double durationS = 0.0;      // travel time to use: measured if there is data, else free-flow
    bool closed = false;
    bool blocked = false;        // incident scenario: road closed, routing must avoid it
};

class TrafficState {
public:
    explicit TrafficState(OsmNetwork& net);   // writes traffic_index into net.graph

    const EdgeTraffic& edge(int e) const { return edges_[e]; }
    void reset();                             // every edge back to free flow

    // Used by applyProvider() and the scenarios: direct access to one edge and to the network.
    EdgeTraffic& mutableEdge(int e) { return edges_[e]; }
    OsmNetwork& network() { return net_; }
    int edgeCount() const { return (int)edges_.size(); }

    // Edge costs like graph_compute_costs(), but with the measured travel times.
    void computeCosts(const Weights& w, std::vector<double>& out) const;

private:
    OsmNetwork& net_;
    std::vector<EdgeTraffic> edges_;
};

}  // namespace rl
