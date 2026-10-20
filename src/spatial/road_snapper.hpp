#ifndef ROUTELENS_ROAD_SNAPPER_HPP
#define ROUTELENS_ROAD_SNAPPER_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "../data/osm_loader.hpp"
#include "kdtree.hpp"

namespace rl {

struct SnapResult {
    bool found = false;
    int edge = -1;               // index into net.graph->edges (and net.geometry)
    int node = -1;               // routing node: the end of that road nearer the click
    double roadDistanceM = 0;    // click to the nearest point on that road
    LatLon nodePos;              // position of `node`
    std::string roadClass;       // OSM highway tag of the road
};

// Snaps a clicked point to the nearest road of an OSM network using the
// KD-tree. The tree holds points spaced along every road shape, so the nearest
// *road* is found even in the middle of a long straight road, not only the
// nearest intersection. Coordinates are projected to local metres first,
// because a degree of longitude is shorter than a degree of latitude.
//
// The network must outlive the snapper. snap() is const and thread-safe.
class RoadSnapper {
public:
    explicit RoadSnapper(const OsmNetwork& net, double spacingM = 20.0);

    // found == false if the network is empty or the nearest road is farther
    // than maxDistanceM (roadDistanceM and edge are still filled in then).
    SnapResult snap(LatLon p, double maxDistanceM) const;

    std::size_t pointCount() const { return pointCount_; }

private:
    const OsmNetwork& net_;
    double cosLat_;
    std::size_t pointCount_ = 0;
    routelens::KDTree tree_;

    routelens::Point project(LatLon p) const;
    double distanceToEdgeM(const routelens::Point& click, std::size_t edge) const;
};

}  // namespace rl

#endif
