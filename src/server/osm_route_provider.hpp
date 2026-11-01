#ifndef ROUTELENS_OSM_ROUTE_PROVIDER_HPP
#define ROUTELENS_OSM_ROUTE_PROVIDER_HPP

#include "route_provider.hpp"
#include "../data/osm_loader.hpp"

#include <vector>

namespace rl {

class OsmRouteProvider final : public RouteProvider {
public:
    explicit OsmRouteProvider(const OsmNetwork& network);

    bool available() const override;
    std::vector<RouteInfo> compute(const RouteRequest& request) override;

private:
    const OsmNetwork& network_;
    std::vector<int> degree_;
};

}  // namespace rl

#endif
