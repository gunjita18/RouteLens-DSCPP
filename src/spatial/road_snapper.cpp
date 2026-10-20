#include "road_snapper.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

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

routelens::Point toXY(LatLon p, double cosLat) {
    return {kEarthRadiusM * cosLat * p.lon * kDegToRad, kEarthRadiusM * p.lat * kDegToRad, 0};
}

double distance(const routelens::Point& a, const routelens::Point& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

// Distance from p to segment ab (all in metres).
double pointToSegment(const routelens::Point& p, const routelens::Point& a,
                      const routelens::Point& b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    if (len2 == 0.0) {
        return distance(p, a);
    }
    const double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0);
    return distance(p, {a.x + t * dx, a.y + t * dy, 0});
}

// The road's own vertices plus extra points so none are more than spacingM
// apart, each tagged with the edge index.
std::vector<routelens::Point> buildPoints(const OsmNetwork& net, double cosLat,
                                          double spacingM) {
    std::vector<routelens::Point> points;
    for (std::size_t e = 0; e < net.geometry.size(); ++e) {
        const std::vector<LatLon>& shape = net.geometry[e];
        for (std::size_t i = 0; i < shape.size(); ++i) {
            routelens::Point a = toXY(shape[i], cosLat);
            a.id = e;
            points.push_back(a);
            if (i + 1 == shape.size()) {
                break;
            }
            const routelens::Point b = toXY(shape[i + 1], cosLat);
            const int steps = static_cast<int>(std::ceil(distance(a, b) / spacingM));
            for (int k = 1; k < steps; ++k) {
                const double t = static_cast<double>(k) / steps;
                points.push_back({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), e});
            }
        }
    }
    return points;
}

routelens::KDTree makeTree(const OsmNetwork& net, double cosLat, double spacingM,
                           std::size_t& count) {
    std::vector<routelens::Point> points = buildPoints(net, cosLat, std::max(spacingM, 1.0));
    count = points.size();
    return routelens::KDTree(std::move(points));
}

}  // namespace

RoadSnapper::RoadSnapper(const OsmNetwork& net, double spacingM)
    : net_(net),
      cosLat_(meanCosLat(net)),
      tree_(makeTree(net, cosLat_, spacingM, pointCount_)) {}

routelens::Point RoadSnapper::project(LatLon p) const {
    return toXY(p, cosLat_);
}

double RoadSnapper::distanceToEdgeM(const routelens::Point& click, std::size_t edge) const {
    const std::vector<LatLon>& shape = net_.geometry[edge];
    double best = distance(click, project(shape.front()));
    for (std::size_t i = 0; i + 1 < shape.size(); ++i) {
        best = std::min(best, pointToSegment(click, project(shape[i]), project(shape[i + 1])));
    }
    return best;
}

SnapResult RoadSnapper::snap(LatLon p, double maxDistanceM) const {
    SnapResult out;
    if (tree_.empty() || net_.graph == nullptr) {
        return out;
    }
    const routelens::Point click = project(p);
    const std::size_t edge = tree_.nearest(click).id;

    out.edge = static_cast<int>(edge);
    out.roadDistanceM = distanceToEdgeM(click, edge);
    out.roadClass = net_.roadClass[edge];

    const Edge& e = net_.graph->edges[edge];
    const bool fromCloser =
        distance(click, project(net_.pos[e.from])) <= distance(click, project(net_.pos[e.to]));
    out.node = fromCloser ? e.from : e.to;
    out.nodePos = net_.pos[out.node];
    out.found = out.roadDistanceM <= maxDistanceM;
    return out;
}

}  // namespace rl
