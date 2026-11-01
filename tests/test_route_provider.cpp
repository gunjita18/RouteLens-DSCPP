
// Route provider on a small hand-built network: penalty-method alternatives,
// what-if scenarios (blocked and slowed roads), major-junction waits and
// traffic scenarios (rain, rush hour).

#include "../src/server/osm_route_provider.hpp"
#include "../src/traffic/scenario.hpp"

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
        // No junction list: no waits, so the ETA is the driving time.
        assert(std::abs(routes[0].etaSeconds - 400.0) < 1e-6);
        assert(std::abs(routes[1].etaSeconds - 500.0) < 1e-6);
        assert(routes[0].junctions == 0 && routes[1].junctions == 0);
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
        assert(std::abs(routes[0].etaSeconds - 500.0) < 1e-6);
        assert(std::abs(routes[1].etaSeconds - 600.0) < 1e-6);
        // A closure is physical, so the "no traffic" ETA includes it too.
        assert(std::abs(routes[1].freeFlowEtaSeconds - 600.0) < 1e-6);
    }

    // ---- major junctions: counted once, 30 s wait, and the search sees it ----
    {
        // One documented junction covering node 1 (the north road's middle).
        rl::JunctionIndex idx;
        rl::Junction j;
        j.name = "North Chowk";
        j.pos = f.net.pos[1];
        j.nodes = {1};
        idx.junctions.push_back(j);
        idx.junctionAtNode = {-1, 0, -1, -1};
        rl::OsmRouteProvider withJunctions(f.net, &idx);

        std::vector<rl::RouteInfo> routes = withJunctions.compute(request());
        assert(routes.size() == 2);
        assert(passesThrough(routes[0], f.net.pos[1]));  // north still faster: 430 < 500
        assert(routes[0].junctions == 1 && routes[1].junctions == 0);
        assert(std::abs(routes[0].etaSeconds - 430.0) < 1e-6);
        assert(std::abs(routes[0].freeFlowEtaSeconds - 430.0) < 1e-6);
        assert(std::abs(routes[1].etaSeconds - 500.0) < 1e-6);

        // North slowed to 480 s of driving: 480 + 30 > 500, so the junction
        // wait decides it. Without the wait in the search north would win.
        rl::RouteRequest r = request();
        r.durationFactor.assign(f.net.graph->edge_count, 1.0);
        r.durationFactor[f.north2] = 1.4;  // 200 + 280 = 480 s
        routes = withJunctions.compute(r);
        assert(routes.size() == 2);
        assert(passesThrough(routes[0], f.net.pos[2]));  // south recommended
        assert(std::abs(routes[0].etaSeconds - 500.0) < 1e-6);
        assert(std::abs(routes[1].etaSeconds - 510.0) < 1e-6);

        // Rain slows every road by the same factor, so with all four priorities
        // the recommendation must not change: congestion is equal on both
        // routes and rounding noise in it must not count as a difference.
        rl::RouteRequest all = request();
        all.wTime = all.wDistance = all.wJunctions = all.wTraffic = 0.25;
        const std::vector<rl::RouteInfo> dry = withJunctions.compute(all);
        all.scenario = "rain";
        const std::vector<rl::RouteInfo> wet = withJunctions.compute(all);
        assert(dry.size() == 2 && wet.size() == 2);
        assert(passesThrough(dry[0], f.net.pos[1]) && passesThrough(wet[0], f.net.pos[1]));
        assert(std::abs(dry[0].score - wet[0].score) < 1e-9);
    }

    // ---- traffic scenarios: rain slows everything, rush hour main roads most ----
    {
        // North road primary (a main road), south road residential.
        f.net.roadClass.assign(f.net.graph->edge_count, "residential");
        f.net.roadClass[f.north1] = "primary";
        f.net.roadClass[f.north2] = "primary";
        f.net.roadClass[4] = "primary";   // 1 -> 0
        f.net.roadClass[5] = "primary";   // 3 -> 1
        const rl::ScenarioSettings settings;

        rl::RouteRequest r = request();
        r.scenario = "rain";
        std::vector<rl::RouteInfo> routes = provider.compute(r);
        assert(routes.size() == 2);
        assert(passesThrough(routes[0], f.net.pos[1]));  // same order: all roads x 0.88
        assert(std::abs(routes[0].etaSeconds - 400.0 / settings.rainFactor) < 1e-6);
        assert(std::abs(routes[0].freeFlowEtaSeconds - 400.0) < 1e-6);   // without the rain
        assert(routes[0].trafficLevel == std::string("Low (rain estimate)"));

        r.scenario = "rush_hour";   // north 400 / 0.65 = 615 s, south 500 / 0.85 = 588 s
        routes = provider.compute(r);
        assert(routes.size() == 2);
        assert(passesThrough(routes[0], f.net.pos[2]));  // the side road now wins
        assert(std::abs(routes[0].etaSeconds - 500.0 / settings.rushOtherFactor) < 1e-6);
        assert(std::abs(routes[1].etaSeconds - 400.0 / settings.rushMainFactor) < 1e-6);
        assert(std::abs(routes[1].freeFlowEtaSeconds - 400.0) < 1e-6);

        r.scenario = "incident";   // an incident is a closure the user places
        routes = provider.compute(r);
        assert(std::abs(routes[0].etaSeconds - 400.0) < 1e-6);

        // Closures and rain combine: south blocked, north in the rain.
        r = request();
        r.scenario = "rain";
        r.blockedEdges.assign(f.net.graph->edge_count, false);
        r.blockedEdges[f.south1] = true;
        routes = provider.compute(r);
        assert(routes.size() == 1);
        assert(std::abs(routes[0].etaSeconds - 400.0 / settings.rainFactor) < 1e-6);

        // The provider never writes to the shared network (requests run in parallel).
        for (int e = 0; e < f.net.graph->edge_count; ++e) {
            assert(f.net.graph->edges[e].traffic_index == 0.0);
        }
        f.net.roadClass.clear();
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
