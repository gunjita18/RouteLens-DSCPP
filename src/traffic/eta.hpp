// eta.hpp — route summary for display: ETA, distance, signals, traffic level,
// and how much of the route is covered by live / typical / estimated data.
//
// ETA rule: on edges with TomTom data, TomTom's speed already includes waiting
// at signals, so nothing is added. On free-flow edges, signalDelayS is added per
// signal. This way signal waiting time is never counted twice.
// With a JunctionIndex, documented major junctions replace OSM signals for the
// wait (OSM has only 6 signals mapped in Dehradun); same no-double-counting rule.
#pragma once
#include <string>
#include <vector>

#include "junctions.hpp"
#include "traffic.hpp"

namespace rl {

struct RouteSummary {
    double distanceM = 0;
    double freeFlowS = 0;      // no traffic data at all: free-flow + signal delays
    double etaS = 0;           // traffic-aware ETA
    int signals = 0;           // unique mapped signals on the route
    int junctions = 0;         // major junctions on the route (when a JunctionIndex is given)
    std::vector<std::string> junctionNames;   // in route order
    double liveShare = 0, historicalShare = 0, estimatedShare = 0;   // fractions of route length
    double meanRatio = 1.0;    // length-weighted current/free-flow over edges with data
    TrafficLevel level = TrafficLevel::Unknown;   // Unknown if no edge has data
    bool anyClosure = false;
};

RouteSummary summarizeRoute(const OsmNetwork& net, const TrafficState& state, const std::vector<int>& routeEdges,
                            const TrafficRules& rules, double signalDelayS,
                            const JunctionIndex* junctions = nullptr);

// "< 1 min", "37 min", "1 h 5 min", "2 h"
std::string formatDuration(double seconds);
// "850 m", "9.4 km"
std::string formatDistance(double meters);
// e.g. "live on 45% of the route, typical on 10%, estimated on 45%"
std::string coverageLabel(const RouteSummary& s);

}  // namespace rl
