// geo.hpp — small geodesy toolkit: distances, point-to-polyline buffer test,
// bounding boxes and Web Mercator (slippy-map tile) projection.
#pragma once
#include <vector>

namespace rl {

struct LatLon {
    double lat = 0.0;
    double lon = 0.0;
};

struct Signal {
    long long id = 0;   // OSM node id
    LatLon pos;
};

struct BBox {
    double south = 90, west = 180, north = -90, east = -180;
    bool valid() const { return south <= north && west <= east; }
    void extend(LatLon p);
    BBox padded(double meters) const;
    bool contains(LatLon p) const;
};

constexpr double kEarthRadiusM = 6371008.8;   // mean Earth radius (IUGG)

double haversineMeters(LatLon a, LatLon b);

// Shortest distance from p to segment ab. Uses a local equirectangular
// projection centred on p — error is negligible at the ~20 m scale we test.
double pointToSegmentMeters(LatLon p, LatLon a, LatLon b);
double pointToPolylineMeters(LatLon p, const std::vector<LatLon>& line);

// OSM ids of signals within bufferM of the polyline (sorted, unique).
std::vector<long long> signalsNearPolyline(const std::vector<Signal>& signals,
                                           const std::vector<LatLon>& line,
                                           double bufferM);

// Web Mercator, 256-px tiles: "world pixel" coordinates at zoom z.
double lonToWorldX(double lon, int z);
double latToWorldY(double lat, int z);
double worldXToLon(double x, int z);
double worldYToLat(double y, int z);

// Largest integer zoom at which bbox fits into widthPx x heightPx.
int fitZoom(const BBox& box, double widthPx, double heightPx, int minZ = 3, int maxZ = 17);

}  // namespace rl
