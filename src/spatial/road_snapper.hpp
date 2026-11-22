#ifndef ROUTELENS_ROAD_SNAPPER_HPP
#define ROUTELENS_ROAD_SNAPPER_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "../data/osm_loader.hpp"

namespace rl {

struct SnapResult {
    bool found = false;
    int edge = -1;               // index into net.graph->edges (and net.geometry)
    int node = -1;               // routing node: the end of that road nearer the click
    double roadDistanceM = 0;    // click to the nearest point on that road
    LatLon nodePos;              // position of `node`
    std::string roadClass;       // OSM highway tag of the road
};

// Snaps a clicked point to the nearest road of an OSM network with a linear
// search (Phase III design; the KD-tree used in Phase II was removed).
//
// Every road is stored as a shape: a list of points joined by straight pieces
// (segments). snap() loops over every segment of every road, measures the
// distance from the click to that segment, and keeps the smallest. The answer
// is exact, and because it measures to the segment, a click in the middle of
// a long straight road finds that road, not a farther crossing.
//
// Cost: O(S) per click, S = number of segments (about 0.2 million for
// Dehradun), which takes a few milliseconds. Coordinates are first converted
// to local metres, because a degree of longitude is shorter than a degree of
// latitude; the converted segments are computed once in the constructor.
//
// The network must outlive the snapper. snap() is const and thread-safe.
class RoadSnapper {
public:
    explicit RoadSnapper(const OsmNetwork& net);

    // found == false if the network is empty or the nearest road is farther
    // than maxDistanceM (roadDistanceM and edge are still filled in then).
    SnapResult snap(LatLon p, double maxDistanceM) const;

    // Number of straight road pieces the linear search checks per click.
    std::size_t segmentCount() const { return segments_.size(); }

private:
    struct XY {
        double x, y;   // metres east and north
    };
    struct Segment {
        XY a, b;
        int edge;      // road this piece belongs to
    };

    const OsmNetwork& net_;
    double cosLat_;
    std::vector<Segment> segments_;

    XY project(LatLon p) const;
};

}  // namespace rl

#endif
