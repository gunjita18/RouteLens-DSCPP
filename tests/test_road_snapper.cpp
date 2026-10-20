#include "road_snapper.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

using rl::LatLon;
using rl::OsmNetwork;
using rl::RoadSnapper;
using rl::SnapResult;

namespace {

// Independent reference: distance from p to the road shape, in local metres,
// using the same projection idea but written separately from the snapper.
double refDistanceM(LatLon p, const std::vector<LatLon>& shape, double cosLat) {
    auto xy = [&](LatLon q) {
        const double k = 3.14159265358979323846 / 180.0;
        return std::pair<double, double>{rl::kEarthRadiusM * cosLat * q.lon * k,
                                         rl::kEarthRadiusM * q.lat * k};
    };
    const auto c = xy(p);
    double best = 1e18;
    for (std::size_t i = 0; i + 1 < shape.size(); ++i) {
        const auto a = xy(shape[i]);
        const auto b = xy(shape[i + 1]);
        const double dx = b.first - a.first, dy = b.second - a.second;
        const double len2 = dx * dx + dy * dy;
        double t = len2 == 0 ? 0 : ((c.first - a.first) * dx + (c.second - a.second) * dy) / len2;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        best = std::min(best, std::hypot(c.first - (a.first + t * dx), c.second - (a.second + t * dy)));
    }
    return best;
}

// Small hand-made network (no CSV files needed):
//   A(30.300,78.000) --- B(30.300,78.020)    long straight road, ~1.9 km, no inner vertices
//   C(30.303,78.010) --- D(30.303,78.011)    short road ~95 m, north of AB's middle
void addRoad(OsmNetwork& net, int a, int b, const char* cls) {
    graph_add_edge(net.graph, a, b, 100.0, 10.0, 0.0, nullptr, 0);
    net.geometry.push_back({net.pos[a], net.pos[b]});
    net.roadClass.push_back(cls);
}

void buildTinyNetwork(OsmNetwork& net) {
    net.graph = graph_create(4);
    net.pos = {{30.300, 78.000}, {30.300, 78.020}, {30.303, 78.010}, {30.303, 78.011}};
    net.osmId = {1, 2, 3, 4};
    net.isSignal = {0, 0, 0, 0};
    addRoad(net, 0, 1, "primary");
    addRoad(net, 2, 3, "residential");
}

}  // namespace

int main() {
    int checks = 0;
    int failures = 0;
    auto check = [&](bool condition, const char* message) {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    };

    // ---- tiny network ------------------------------------------------------
    {
        OsmNetwork net;
        buildTinyNetwork(net);
        RoadSnapper snap(net, 20.0);
        check(snap.pointCount() > 50, "Long road is densified into many points");

        // Click 55 m north of the middle of AB. The nearest *node* is far away
        // (~1 km) and the nearest *vertex of another road* (C, ~280 m) is closer
        // than A or B, but the nearest road is still AB.
        SnapResult r = snap.snap({30.3005, 78.010}, 500.0);
        check(r.found, "Click near a long road is snapped");
        check(r.edge == 0, "Nearest road wins over nearer vertices of another road");
        check(r.roadClass == "primary", "Road class is reported");
        check(std::abs(r.roadDistanceM - 55.6) < 3.0, "Distance to the road is about 55 m");

        // Near the west end the routing node is A, near the east end it is B.
        check(snap.snap({30.3002, 78.002}, 500.0).node == 0, "Snaps to the nearer end (A)");
        check(snap.snap({30.3002, 78.018}, 500.0).node == 1, "Snaps to the nearer end (B)");

        // Far from every road.
        SnapResult far = snap.snap({30.360, 78.010}, 500.0);
        check(!far.found, "Click far from any road is rejected");
        check(far.roadDistanceM > 500.0, "Rejected click still reports its distance");

        // A click just past the east end of the short road (about 72 m from D).
        SnapResult east = snap.snap({30.3034, 78.0116}, 500.0);
        check(east.found && east.edge == 1 && east.node == 3, "Click past the east end snaps to D");
    }

    // ---- empty network -----------------------------------------------------
    {
        OsmNetwork net;
        RoadSnapper snap(net, 20.0);
        check(!snap.snap({30.3, 78.0}, 500.0).found, "Empty network never finds a road");
    }

    // ---- real Dehradun data, only if the CSVs are present --------------------
    const char* dirEnv = std::getenv("ROUTELENS_DATA_DIR");
    const std::string dir = dirEnv ? dirEnv : "data/osm";
    if (std::ifstream(dir + "/nodes.csv") && std::ifstream(dir + "/edges.csv")) {
        OsmNetwork net;
        std::string err;
        check(rl::loadOsmNetwork(dir, net, err), "Real network loads");
        if (err.empty() && net.graph != nullptr) {
            auto t0 = std::chrono::steady_clock::now();
            RoadSnapper snap(net, 20.0);
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - t0).count();
            std::cout << "real network: " << net.stats.keptNodes << " nodes, " << net.stats.keptEdges
                      << " edges, " << snap.pointCount() << " KD-tree points, built in "
                      << static_cast<long>(ms) << " ms\n";

            double cosLat = 0;
            for (const LatLon& p : net.pos) cosLat += p.lat;
            cosLat = std::cos(cosLat / net.pos.size() * 3.14159265358979323846 / 180.0);

            // Every snap must agree with brute force over all edges, within the
            // point spacing (the tree is spaced, brute force is exact).
            std::mt19937 rng(12345);
            std::uniform_real_distribution<double> latD(30.27, 30.40), lonD(77.95, 78.10);
            int compared = 0, worse = 0;
            double worst = 0;
            for (int i = 0; i < 200; ++i) {
                const LatLon p{latD(rng), lonD(rng)};
                SnapResult r = snap.snap(p, 1e9);
                double bf = 1e18;
                for (const auto& shape : net.geometry) bf = std::min(bf, refDistanceM(p, shape, cosLat));
                ++compared;
                const double gap = r.roadDistanceM - bf;
                worst = std::max(worst, gap);
                if (gap > 20.0 + 1e-6) ++worse;
                if (gap < -1e-6) ++worse;   // can never beat the exact minimum
            }
            std::cout << "brute-force comparison: " << compared << " points, worst gap "
                      << worst << " m\n";
            check(worse == 0, "Snapped road is within 20 m of the true nearest road (200 points)");

            // A real landmark: Clock Tower area, Dehradun.
            SnapResult tower = snap.snap({30.3243, 78.0412}, 300.0);
            check(tower.found, "Clock Tower area snaps to a road within 300 m");
            check(tower.node >= 0 && tower.node < net.graph->node_count, "Snapped node is a valid graph node");
        }
    } else {
        std::cout << "real-network checks skipped (no CSVs in " << dir << ")\n";
    }

    std::cout << "Road snapper tests: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
