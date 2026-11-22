#include "road_snapper.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rl {

namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

// Mean latitude of the network; longitude is scaled by its cosine.
double meanCosLat(const OsmNetwork& net) {
    if (net.pos.empty()) {
        return 1.0;
    }
    double sum = 0;
    for (const LatLon& p : net.pos) {
        sum += p.lat;
    }
    return std::cos(sum / static_cast<double>(net.pos.size()) * kDegToRad);
}

}  // namespace

RoadSnapper::RoadSnapper(const OsmNetwork& net) : net_(net), cosLat_(meanCosLat(net)) {
    // Convert every road shape to metres once, as a flat list of segments.
    for (std::size_t e = 0; e < net.geometry.size(); ++e) {
        const std::vector<LatLon>& shape = net.geometry[e];
        if (shape.size() == 1) {   // a road of one point: a segment of length 0
            segments_.push_back({project(shape[0]), project(shape[0]), static_cast<int>(e)});
        }
        for (std::size_t i = 0; i + 1 < shape.size(); ++i) {
            segments_.push_back({project(shape[i]), project(shape[i + 1]), static_cast<int>(e)});
        }
    }
}

RoadSnapper::XY RoadSnapper::project(LatLon p) const {
    return {kEarthRadiusM * cosLat_ * p.lon * kDegToRad, kEarthRadiusM * p.lat * kDegToRad};
}

SnapResult RoadSnapper::snap(LatLon p, double maxDistanceM) const {
    SnapResult out;
    if (segments_.empty() || net_.graph == nullptr) {
        return out;
    }
    const XY c = project(p);

    // Linear search: the segment with the smallest distance to the click.
    // Squared distances are compared (no square root inside the loop).
    double bestSq = std::numeric_limits<double>::infinity();
    int bestEdge = -1;
    for (const Segment& s : segments_) {
        const double dx = s.b.x - s.a.x;
        const double dy = s.b.y - s.a.y;
        const double len2 = dx * dx + dy * dy;
        // t = how far along the segment the closest point lies (0 = a, 1 = b).
        double t = 0.0;
        if (len2 > 0.0) {
            t = std::clamp(((c.x - s.a.x) * dx + (c.y - s.a.y) * dy) / len2, 0.0, 1.0);
        }
        const double ex = c.x - (s.a.x + t * dx);
        const double ey = c.y - (s.a.y + t * dy);
        const double d2 = ex * ex + ey * ey;
        if (d2 < bestSq) {
            bestSq = d2;
            bestEdge = s.edge;
        }
    }

    out.edge = bestEdge;
    out.roadDistanceM = std::sqrt(bestSq);
    out.roadClass = net_.roadClass[bestEdge];

    // Routing works on nodes, so use the end of that road nearer the click.
    const Edge& e = net_.graph->edges[bestEdge];
    const XY from = project(net_.pos[e.from]);
    const XY to = project(net_.pos[e.to]);
    const double dFrom = std::hypot(c.x - from.x, c.y - from.y);
    const double dTo = std::hypot(c.x - to.x, c.y - to.y);
    out.node = dFrom <= dTo ? e.from : e.to;
    out.nodePos = net_.pos[out.node];
    out.found = out.roadDistanceM <= maxDistanceM;
    return out;
}

}  // namespace rl
