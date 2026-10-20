// traffic.hpp — live traffic on the OSM road graph (no network code here).
//
//   FlowSegment     one TomTom Traffic Flow answer: speeds + the road shape it covers
//   TrafficRules    every threshold in one place (all documented, all tunable)
//   TrafficState    per-edge traffic for the graph; free-flow unless a segment matched
//
// Matching rule: a TomTom segment applies to one of OUR edges only if the edge
// lies along the segment (25/50/75% points within matchM metres) AND points the
// same way (bearing difference <= maxBearingDiffDeg). Opposite-direction edges
// are not matched: their traffic can differ, so they stay on free-flow.
#pragma once
#include <string>
#include <vector>

#include "../core/graph.h"
#include "../data/geo.hpp"
#include "../data/osm_loader.hpp"

namespace rl {

enum class TrafficLevel { Low, Medium, High, Unknown };
enum class DataSource { FreeFlow, Historical, Live };

struct TrafficRules {
    double maxSnapM = 50.0;            // TomTom answer for a road farther than this: not our road
    double matchM = 20.0;              // edge must lie within this distance of the segment
    double maxBearingDiffDeg = 45.0;   // and point in the same direction
    double minConfidence = 0.5;        // below: ignore (TomTom: 0.5 = historical only)
    double liveConfidence = 0.6;       // above: live probe data (TomTom documentation)
    double lowRatio = 0.8;             // current/free-flow >= 0.8 -> Low   (our design choice)
    double highRatio = 0.5;            // current/free-flow <  0.5 -> High  (our design choice)
};

struct FlowSegment {
    std::string frc;                   // TomTom road class, FRC0 (motorway) .. FRC6 (local)
    double currentKph = 0, freeFlowKph = 0;
    double currentTravelS = 0, freeFlowTravelS = 0;
    double confidence = 0;
    bool roadClosure = false;
    std::vector<LatLon> shape;         // ordered in the direction of travel
};

// Parses a flowSegmentData JSON response. Returns false and sets err on failure.
bool parseFlowSegment(const std::string& json, FlowSegment& out, std::string& err);

TrafficLevel levelForRatio(double ratio, const TrafficRules& r);   // ratio = current / free-flow
DataSource sourceForConfidence(double confidence, const TrafficRules& r);
const char* levelName(TrafficLevel level);
const char* sourceName(DataSource source);

double bearingDeg(LatLon a, LatLon b);          // 0 = north, 90 = east
double angleDiffDeg(double a, double b);        // smallest difference, 0..180
double polylineLengthM(const std::vector<LatLon>& line);
LatLon pointAlong(const std::vector<LatLon>& line, double fraction, double* bearingOut = nullptr);

// Distance from p to the segment shape (metres).
double distanceToSegmentM(LatLon p, const FlowSegment& seg);

// True if the edge (its road shape) lies along the segment in the same direction.
// directionRejected (optional) is set when the edge is close enough but opposite.
bool edgeMatchesSegment(const std::vector<LatLon>& edgeShape, const FlowSegment& seg,
                        const TrafficRules& r, bool* directionRejected = nullptr);

struct EdgeTraffic {
    DataSource source = DataSource::FreeFlow;
    double ratio = 1.0;          // current / free-flow speed (1.0 when no data)
    double confidence = 0.0;
    double durationS = 0.0;      // travel time to use: live/historical if matched, else free-flow
    bool closed = false;
};

class TrafficState {
public:
    explicit TrafficState(OsmNetwork& net);   // writes traffic_index into net.graph

    // Applies seg to the candidate edges (e.g. one route's edges). Writes the
    // congestion into graph->edges[e].traffic_index so graph_compute_costs()
    // sees it. Returns the number of edges updated.
    int applySegment(const FlowSegment& seg, const TrafficRules& r, const std::vector<int>& candidates);

    const EdgeTraffic& edge(int e) const { return edges_[e]; }
    int directionRejections() const { return directionRejected_; }
    void reset();

    // Edge costs like graph_compute_costs(), but with live/historical travel times.
    void computeCosts(const Weights& w, std::vector<double>& out) const;

private:
    OsmNetwork& net_;
    std::vector<EdgeTraffic> edges_;
    int directionRejected_ = 0;
};

// Points every spacingM metres along the route, only on road classes TomTom
// covers (residential/service streets are skipped: no data there, saves quota).
std::vector<LatLon> sampleRoute(const OsmNetwork& net, const std::vector<int>& routeEdges, double spacingM);
bool tomtomCoversRoadClass(const std::string& highway);

}  // namespace rl
