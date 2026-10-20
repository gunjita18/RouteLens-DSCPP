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
    double etaMinutes = 0;
    double distanceKm = 0;
    int signals = 0;
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
