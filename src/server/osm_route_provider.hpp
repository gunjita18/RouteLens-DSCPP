#ifndef ROUTELENS_OSM_ROUTE_PROVIDER_HPP
#define ROUTELENS_OSM_ROUTE_PROVIDER_HPP

#include "route_provider.hpp"
#include "../data/osm_loader.hpp"
#include "../oop/traffic_provider.hpp"
#include "../traffic/junctions.hpp"

#include <vector>

namespace rl {

class OsmRouteProvider final : public RouteProvider {
public:
    // junctions: the documented major junctions (may be null). With them, each
    // route counts its major junctions and waits there; without, it waits nowhere.
    //
    // traffic: where measured traffic comes from (may be null, or a
    // FreeFlowProvider: then every edge is at free-flow speed). It is read ONCE,
    // here, into per-edge arrays; the normal and incident scenarios use them, the
    // rain and rush-hour scenarios ignore measured data and start from free flow
    // (docs/TRAFFIC.md section 13). The provider need not outlive the constructor.
    //
    // The provider never writes to the network, and its arrays never change after
    // construction, so concurrent requests are safe.
    explicit OsmRouteProvider(const OsmNetwork& network,
                              const JunctionIndex* junctions = nullptr,
                              const TrafficProvider* traffic = nullptr);

    bool available() const override;
    std::vector<RouteInfo> compute(const RouteRequest& request) override;

    // Number of edges that carry measured traffic data.
    int edgesWithTraffic() const { return edgesWithTraffic_; }

private:
    const OsmNetwork& network_;
    const JunctionIndex* junctions_;

    // Per graph edge, from the traffic provider (empty when there is no data):
    //   travel time on the edge = free-flow time x snapshotFactor_[e]
    //   snapshotIndex_[e]       = congestion 0 (free) .. 1 (standstill)
    std::vector<double> snapshotFactor_;
    std::vector<double> snapshotIndex_;
    std::vector<char> snapshotHasData_;
    int edgesWithTraffic_ = 0;
};

}  // namespace rl

#endif
