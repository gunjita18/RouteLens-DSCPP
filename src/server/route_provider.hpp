#ifndef ROUTELENS_ROUTE_PROVIDER_HPP
#define ROUTELENS_ROUTE_PROVIDER_HPP

#include <string>
#include <utility>
#include <vector>

#include "request_params.hpp"

namespace rl {

// One alternative route as shown on a route card and drawn on the map.
struct RouteInfo {
    std::string name;
    std::vector<std::pair<double, double>> polyline;  // (lat, lon)
    double etaSeconds = 0;          // with traffic; the website formats it
    double freeFlowEtaSeconds = 0;  // same route with no traffic, to show the effect
    double distanceKm = 0;
    // Major junctions, not "signals": OSM maps very few signals in Dehradun.
    int junctions = 0;
    // Share of the route (0-100) by data source, so live data is not overstated.
    double livePct = 0;
    double typicalPct = 0;
    double estimatedPct = 0;
    std::string trafficLevel;  // "Low" | "Medium" | "High"
    double score = 0;          // lower is better, from the user's priorities
    bool recommended = false;
};

// Seam between the web server (Aayush) and the routing engine (routing
// algorithms owner). The server only depends on this interface, so the
// engine can be connected without changing any server or website code.
class RouteProvider {
public:
    virtual ~RouteProvider() = default;
    virtual bool available() const = 0;
    virtual std::vector<RouteInfo> compute(const RouteRequest& request) = 0;
};

// Default provider until the routing engine is connected.
class NullRouteProvider : public RouteProvider {
public:
    bool available() const override { return false; }
    std::vector<RouteInfo> compute(const RouteRequest&) override { return {}; }
};

}  // namespace rl

#endif
