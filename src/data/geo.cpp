#include "geo.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rl {

static constexpr double kPi = 3.14159265358979323846;
static double rad(double d) { return d * kPi / 180.0; }
static double deg(double r) { return r * 180.0 / kPi; }

void BBox::extend(LatLon p) {
    south = std::min(south, p.lat);
    north = std::max(north, p.lat);
    west = std::min(west, p.lon);
    east = std::max(east, p.lon);
}

BBox BBox::padded(double meters) const {
    if (!valid()) return *this;
    double dLat = deg(meters / kEarthRadiusM);
    double midLat = rad((south + north) / 2.0);
    double dLon = dLat / std::max(0.01, std::cos(midLat));
    return BBox{ south - dLat, west - dLon, north + dLat, east + dLon };
}

bool BBox::contains(LatLon p) const {
    return p.lat >= south && p.lat <= north && p.lon >= west && p.lon <= east;
}

double haversineMeters(LatLon a, LatLon b) {
    double dLat = rad(b.lat - a.lat), dLon = rad(b.lon - a.lon);
    double h = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(rad(a.lat)) * std::cos(rad(b.lat)) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2.0 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(h)));
}

double pointToSegmentMeters(LatLon p, LatLon a, LatLon b) {
    // Project to a local plane in metres with p at the origin.
    double k = std::cos(rad(p.lat));
    double ax = rad(a.lon - p.lon) * k * kEarthRadiusM, ay = rad(a.lat - p.lat) * kEarthRadiusM;
    double bx = rad(b.lon - p.lon) * k * kEarthRadiusM, by = rad(b.lat - p.lat) * kEarthRadiusM;
    double dx = bx - ax, dy = by - ay;
    double len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? std::clamp(-(ax * dx + ay * dy) / len2, 0.0, 1.0) : 0.0;
    double cx = ax + t * dx, cy = ay + t * dy;
    return std::sqrt(cx * cx + cy * cy);
}

double pointToPolylineMeters(LatLon p, const std::vector<LatLon>& line) {
    if (line.empty()) return std::numeric_limits<double>::infinity();
    if (line.size() == 1) return haversineMeters(p, line[0]);
    double best = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i + 1 < line.size(); i++)
        best = std::min(best, pointToSegmentMeters(p, line[i], line[i + 1]));
    return best;
}

std::vector<long long> signalsNearPolyline(const std::vector<Signal>& signals,
                                           const std::vector<LatLon>& line,
                                           double bufferM) {
    std::vector<long long> ids;
    if (line.empty()) return ids;
    BBox box;
    for (const auto& p : line) box.extend(p);
    BBox search = box.padded(bufferM * 1.5);   // cheap pre-filter before the exact test
    for (const auto& s : signals) {
        if (!search.contains(s.pos)) continue;
        if (pointToPolylineMeters(s.pos, line) <= bufferM) ids.push_back(s.id);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

double lonToWorldX(double lon, int z) {
    return (lon + 180.0) / 360.0 * 256.0 * std::ldexp(1.0, z);
}

double latToWorldY(double lat, int z) {
    lat = std::clamp(lat, -85.05112878, 85.05112878);
    double s = std::sin(rad(lat));
    return (0.5 - std::log((1 + s) / (1 - s)) / (4 * kPi)) * 256.0 * std::ldexp(1.0, z);
}

double worldXToLon(double x, int z) {
    return x / (256.0 * std::ldexp(1.0, z)) * 360.0 - 180.0;
}

double worldYToLat(double y, int z) {
    double n = kPi - 2.0 * kPi * y / (256.0 * std::ldexp(1.0, z));
    return deg(std::atan(std::sinh(n)));
}

int fitZoom(const BBox& box, double widthPx, double heightPx, int minZ, int maxZ) {
    if (!box.valid()) return minZ;
    for (int z = maxZ; z > minZ; z--) {
        double w = lonToWorldX(box.east, z) - lonToWorldX(box.west, z);
        double h = latToWorldY(box.south, z) - latToWorldY(box.north, z);
        if (w <= widthPx && h <= heightPx) return z;
    }
    return minZ;
}

}  // namespace rl
