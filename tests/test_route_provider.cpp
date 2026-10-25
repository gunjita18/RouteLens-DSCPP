
// Route provider on a small hand-built network: penalty-method alternatives,
// and what-if scenarios (blocked and slowed roads).

#include "../src/server/osm_route_provider.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

// Node 0 (south-west) to node 3 (north-east) by two disjoint roads:
//   north road: 0 -> 1 -> 3, 2.0 km + 2.0 km
//   south road: 0 -> 2 -> 3, 2.5 km + 2.5 km
// Node 4 is an extra junction on the north road so it has interior vertices.
struct Fixture {
    rl::OsmNetwork net;
    int north1 = -1, north2 = -1, south1 = -1, south2 = -1;
};

void build(Fixture& f) {
    Graph* g = graph_create(4);
    assert(g != nullptr);
    f.net.graph = g;
    f.net.pos = {
        {30.300, 78.000},  // 0 start
        {30.330, 78.030},  // 1 north waypoint
        {30.300, 78.040},  // 2 south waypoint
        {30.330, 78.070},  // 3 destination
    };
    f.net.osmId = {10, 11, 12, 13};
    f.net.isSignal.assign(4, 0);

    f.north1 = graph_add_edge(g, 0, 1, 2000, 200, 0, nullptr, 0);
    f.north2 = graph_add_edge(g, 1, 3, 2000, 200, 0, nullptr, 0);
    f.south1 = graph_add_edge(g, 0, 2, 2500, 250, 0, nullptr, 0);
    f.south2 = graph_add_edge(g, 2, 3, 2500, 250, 0, nullptr, 0);
    graph_add_edge(g, 1, 0, 2000, 200, 0, nullptr, 0);
    graph_add_edge(g, 3, 1, 2000, 200, 0, nullptr, 0);
    graph_add_edge(g, 2, 0, 2500, 250, 0, nullptr, 0);
    graph_add_edge(g, 3, 2, 2500, 250, 0, nullptr, 0);
    assert(f.north1 >= 0 && f.south2 >= 0);
}

bool passesThrough(const rl::RouteInfo& route, const rl::LatLon& p) {
    for (const auto& pt : route.polyline) {
        if (std::abs(pt.first - p.lat) < 1e-6 &&
            std::abs(pt.second - p.lon) < 1e-6) {
            return true;
        }
    }
    return false;
}

rl::RouteRequest request() {
    rl::RouteRequest r;
    r.fromNode = 0;
    r.toNode = 3;
    r.wTime = 1.0;
    r.wDistance = 0.0;
    r.wJunctions = 0.0;
    r.wTraffic = 0.0;
    return r;
}

} // namespace

int main() {
    Fixture f;
    build(f);
    rl::OsmRouteProvider provider(f.net);
    assert(provider.available());

    // ---- normal request: both roads offered, the faster one recommended ----
    {
        std::vector<rl::RouteInfo> routes = provider.compute(request());
        assert(routes.size() == 2);
        assert(routes[0].recommended && !routes[1].recommended);
        assert(passesThrough(routes[0], f.net.pos[1]));  // north road first
        assert(passesThrough(routes[1], f.net.pos[2]));
        assert(std::abs(routes[0].etaSeconds - 420.0) < 1e-6);
        assert(std::abs(routes[1].etaSeconds - 520.0) < 1e-6);
        assert(std::abs(routes[0].distanceKm - 4.0) < 1e-6);
        assert(std::abs(routes[1].distanceKm - 5.0) < 1e-6);
    }

    // ---- blocked road: the only route left is the south road ----
    {
        rl::RouteRequest r = request();
        r.blockedEdges.assign(f.net.graph->edge_count, false);
        r.blockedEdges[f.north1] = true;

        std::vector<rl::RouteInfo> routes = provider.compute(r);
        assert(routes.size() == 1);
        assert(passesThrough(routes[0], f.net.pos[2]));
        assert(!passesThrough(routes[0], f.net.pos[1]));
        assert(std::abs(routes[0].distanceKm - 5.0) < 1e-6);
    }

    // ---- both roads blocked: no route, no crash ----
    {
        rl::RouteRequest r = request();
        r.blockedEdges.assign(f.net.graph->edge_count, false);
        r.blockedEdges[f.north1] = true;
        r.blockedEdges[f.south1] = true;
        assert(provider.compute(r).empty());
    }

    // ---- slowed road: still offered, but takes longer and loses the lead ----
    {
        rl::RouteRequest r = request();
        r.durationFactor.assign(f.net.graph->edge_count, 1.0);
        r.durationFactor[f.north2] = 2.0;  // north: 200 + 400 s vs south 500 s

        std::vector<rl::RouteInfo> routes = provider.compute(r);
        assert(routes.size() == 2);
        assert(passesThrough(routes[0], f.net.pos[2]));  // south now recommended
        assert(routes[0].recommended);
        // Each road has one interior junction, which adds 20 s.
        assert(std::abs(routes[0].etaSeconds - 520.0) < 1e-6);
        assert(std::abs(routes[1].etaSeconds - 620.0) < 1e-6);
    }

    // ---- malformed scenario vectors are rejected ----
    {
        rl::RouteRequest r = request();
        r.blockedEdges.assign(3, false);  // wrong size
        assert(provider.compute(r).empty());

        r = request();
        r.durationFactor.assign(2, 1.0);  // wrong size
        assert(provider.compute(r).empty());
    }

    graph_free(f.net.graph);
    f.net.graph = nullptr;  // OsmNetwork frees its own graph in the destructor

    std::cout << "All route provider tests passed.\n";
    return 0;
}
