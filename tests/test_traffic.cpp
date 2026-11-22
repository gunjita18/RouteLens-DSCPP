// test_traffic.cpp — offline tests for the traffic module: traffic levels,
// display formatting, TrafficState and the ETA summary, with measured traffic
// supplied through a fake TrafficProvider. No network is used.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "../src/oop/traffic_provider.hpp"
#include "../src/traffic/eta.hpp"
#include "fake_traffic.hpp"

namespace fs = std::filesystem;
using namespace rl;
using rl_test::FakeTraffic;

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #c "\n"; } } while (0)
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// ---------------------------------------------------------------- pure rules

static void testRules() {
    TrafficRules r;
    CHECK(levelForRatio(1.0, r) == TrafficLevel::Low);
    CHECK(levelForRatio(0.8, r) == TrafficLevel::Low);
    CHECK(levelForRatio(0.79, r) == TrafficLevel::Medium);     // Ballupur, 33/42
    CHECK(levelForRatio(0.62, r) == TrafficLevel::Medium);     // Survey Chowk, 23/37
    CHECK(levelForRatio(0.5, r) == TrafficLevel::Medium);
    CHECK(levelForRatio(0.49, r) == TrafficLevel::High);
    CHECK(levelForRatio(NAN, r) == TrafficLevel::Unknown);
    CHECK(levelForRatio(-0.1, r) == TrafficLevel::Unknown);
    CHECK(std::string(levelName(TrafficLevel::High)) == "High");
    CHECK(std::string(sourceName(DataSource::Historical)) == "saved snapshot");
    CHECK(std::string(sourceName(DataSource::FreeFlow)) == "estimated (free-flow)");

    CHECK(formatDuration(0) == "< 1 min");
    CHECK(formatDuration(29) == "< 1 min");
    CHECK(formatDuration(59) == "1 min");
    CHECK(formatDuration(2244) == "37 min");
    CHECK(formatDuration(3599) == "1 h");
    CHECK(formatDuration(3900) == "1 h 5 min");
    CHECK(formatDuration(7200) == "2 h");
    CHECK(formatDuration(-1) == "unknown");
    CHECK(formatDistance(850) == "850 m");
    CHECK(formatDistance(9400) == "9.4 km");
}

// ---------------------------------------------------------------- small network
// A --primary--> B --primary--> C --residential--> D   (all two-way)
// Signals at B and D. A..C runs north along lon 78.0; C->D runs east.
static fs::path writeNetwork() {
    fs::path d = fs::temp_directory_path() / "routelens_test_traffic";
    fs::create_directories(d);
    std::ofstream(d / "nodes.csv") << "osm_id,lat,lon,is_signal\n"
                                      "1,30.3000000,78.0000000,0\n"
                                      "2,30.3010000,78.0000000,1\n"
                                      "3,30.3020000,78.0000000,0\n"
                                      "4,30.3020000,78.0010000,1\n";
    std::ofstream(d / "edges.csv") << "u,v,length_m,highway,maxspeed_kph,oneway,geometry\n"
                                      "1,2,111.2,primary,,0,78.000000 30.300000;78.000000 30.301000\n"
                                      "2,1,111.2,primary,,0,78.000000 30.301000;78.000000 30.300000\n"
                                      "2,3,111.2,primary,,0,78.000000 30.301000;78.000000 30.302000\n"
                                      "3,2,111.2,primary,,0,78.000000 30.302000;78.000000 30.301000\n"
                                      "3,4,96.0,residential,,0,78.000000 30.302000;78.001000 30.302000\n"
                                      "4,3,96.0,residential,,0,78.001000 30.302000;78.000000 30.302000\n";
    return d;
}

static int edgeOf(const OsmNetwork& net, long long a, long long b) {
    for (int i = 0; i < net.graph->edge_count; i++)
        if (net.osmId[net.graph->edges[i].from] == a && net.osmId[net.graph->edges[i].to] == b) return i;
    return -1;
}

static void testTrafficState() {
    OsmNetwork net;
    std::string err;
    CHECK(loadOsmNetwork(writeNetwork().string(), net, err));
    if (!net.graph) return;
    int ab = edgeOf(net, 1, 2), ba = edgeOf(net, 2, 1), bc = edgeOf(net, 2, 3);
    CHECK(ab >= 0 && ba >= 0 && bc >= 0);

    TrafficState st(net);
    CHECK(st.edgeCount() == net.graph->edge_count);
    CHECK(st.edge(ab).source == DataSource::FreeFlow && st.edge(ab).ratio == 1.0);
    CHECK(near(st.edge(ab).durationS, net.graph->edges[ab].duration_s, 1e-12));

    // Measured half speed on A->B and B->C only.
    FakeTraffic data;
    data.set(ab, 20.0, 0.5, 0.9);
    data.set(bc, 20.0, 0.5, 0.9);
    CHECK(applyProvider(st, data) == 2);
    CHECK(st.edge(ab).source == DataSource::Historical && near(st.edge(ab).ratio, 0.5, 1e-9));
    CHECK(near(st.edge(ab).confidence, 0.9, 1e-12));
    CHECK(near(st.edge(ab).durationS, 111.2 / (20 / 3.6), 1e-9));        // length / measured speed
    CHECK(near(net.graph->edges[ab].traffic_index, 0.5, 1e-9));
    CHECK(st.edge(ba).source == DataSource::FreeFlow && net.graph->edges[ba].traffic_index == 0.0);
    CHECK(near(st.edge(ba).durationS, net.graph->edges[ba].duration_s, 1e-12));   // no data: unchanged

    // Costs use the measured time: 20.016 s = 0.3336 min on A->B.
    Weights w{ 0.0, 1.0, 0.0, 0.0 };
    std::vector<double> c;
    st.computeCosts(w, c);
    CHECK(near(c[ab], (111.2 / (20 / 3.6)) / 60.0, 1e-9));
    Weights cong{ 0.0, 0.0, 1.0, 0.0 };                                   // congestion priority only
    st.computeCosts(cong, c);
    CHECK(c[ab] > 0.0 && c[ba] == 0.0);

    // reset() returns every edge to free flow.
    st.reset();
    CHECK(st.edge(ab).source == DataSource::FreeFlow && net.graph->edges[ab].traffic_index == 0.0);

    // A measured speed of 0 marks the edge closed: free-flow time kept, congestion maximal.
    FakeTraffic stopped;
    stopped.set(ab, 0.0, 1.0, 1.0);
    CHECK(applyProvider(st, stopped) == 1);
    CHECK(st.edge(ab).closed && near(st.edge(ab).durationS, net.graph->edges[ab].duration_s, 1e-12));
    CHECK(net.graph->edges[ab].traffic_index == 1.0);

    // A provider with no data changes nothing.
    st.reset();
    FreeFlowProvider none;
    CHECK(applyProvider(st, none) == 0 && !st.edge(ab).closed);
}

// ---------------------------------------------------------------- route summary

static void testRouteSummary() {
    OsmNetwork net;
    std::string err;
    CHECK(loadOsmNetwork(writeNetwork().string(), net, err));
    if (!net.graph) return;
    int ab = edgeOf(net, 1, 2), bc = edgeOf(net, 2, 3), cd = edgeOf(net, 3, 4);
    std::vector<int> route = { ab, bc, cd };
    TrafficRules r;

    // Without any traffic data: free-flow + 30 s per signal (B and D).
    TrafficState st(net);
    RouteSummary s0 = summarizeRoute(net, st, route, r, 30.0);
    double ffPrimary = 111.2 / (40 / 3.6), ffRes = 96.0 / (20 / 3.6);
    CHECK(near(s0.distanceM, 318.4, 1e-9) && s0.signals == 2);
    CHECK(near(s0.freeFlowS, 2 * ffPrimary + ffRes + 60, 1e-9) && near(s0.etaS, s0.freeFlowS, 1e-9));
    CHECK(s0.level == TrafficLevel::Unknown);
    CHECK(coverageLabel(s0) == "no traffic data on this route: ETA is an estimate");

    // Measured data on the two primary edges (half speed); the residential edge has none.
    FakeTraffic data;
    data.set(ab, 20.0, 0.5, 0.9);
    data.set(bc, 20.0, 0.5, 0.9);
    CHECK(applyProvider(st, data) == 2);
    RouteSummary s = summarizeRoute(net, st, route, r, 30.0);
    double measured = 111.2 / (20 / 3.6);
    // Signal B is on a measured edge (the wait is already in the measured speed); D is on a free-flow edge.
    CHECK(near(s.etaS, 2 * measured + ffRes + 30, 1e-9));
    CHECK(near(s.historicalShare, 222.4 / 318.4, 1e-9) && near(s.estimatedShare, 96.0 / 318.4, 1e-9));
    CHECK(s.liveShare == 0.0);                                            // a snapshot is never live
    CHECK(near(s.meanRatio, 0.5, 1e-9) && s.level == TrafficLevel::Medium && !s.anyClosure);
    CHECK(coverageLabel(s) == "live on 0% of the route, typical on 70%, estimated on 30%");
}

int main() {
    testRules();
    testTrafficState();
    testRouteSummary();
    std::cout << "test_traffic: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
