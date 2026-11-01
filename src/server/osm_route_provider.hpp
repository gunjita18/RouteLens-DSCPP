#ifndef ROUTELENS_OSM_ROUTE_PROVIDER_HPP
#define ROUTELENS_OSM_ROUTE_PROVIDER_HPP

#include "route_provider.hpp"
#include "../data/osm_loader.hpp"
#include "../traffic/junctions.hpp"

#include <vector>

namespace rl {

class OsmRouteProvider final : public RouteProvider {
public:
    // junctions: the documented major junctions (may be null). With them, each
    // route counts its major junctions and waits there; without, it waits nowhere.
    // The provider never writes to the network, so concurrent requests are safe.
    explicit OsmRouteProvider(const OsmNetwork& network,
                              const JunctionIndex* junctions = nullptr);

    bool available() const override;
    std::vector<RouteInfo> compute(const RouteRequest& request) override;

private:
    const OsmNetwork& network_;
    const JunctionIndex* junctions_;
};

}  // namespace rl

#endif
