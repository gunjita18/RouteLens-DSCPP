// test_junctions.cpp — loading, snapping and per-route counting of major
// junctions, and their effect on the ETA. Offline, small hand-made inputs.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "../src/traffic/eta.hpp"
#include "../src/traffic/junctions.hpp"

namespace fs = std::filesystem;
using namespace rl;

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #c "\n"; } } while (0)
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// A(1) - B(2) - B2(3) - C(4) - D(5) along lon 78.0 / 78.001, all primary, two-way.
// B and B2 are 6 m apart: one junction with two nodes (like a divided road).
static fs::path writeNetwork() {
    fs::path d = fs::temp_directory_path() / "routelens_test_junctions";
    fs::create_directories(d);
    std::ofstream(d / "nodes.csv") << "osm_id,lat,lon,is_signal\n"
                                      "1,30.3000000,78.0000000,0\n"
                                      "2,30.3010000,78.0000000,1\n"
                                      "3,30.3010540,78.0000000,0\n"
                                      "4,30.3020000,78.0000000,0\n"
                                      "5,30.3020000,78.0010000,0\n";
    auto edge = [](std::ofstream& o, int a, int b, double len, double la1, double lo1, double la2, double lo2) {
        o << a << "," << b << "," << len << ",primary,,0," << lo1 << " " << la1 << ";" << lo2 << " " << la2 << "\n";
        o << b << "," << a << "," << len << ",primary,,0," << lo2 << " " << la2 << ";" << lo1 << " " << la1 << "\n";
    };
    std::ofstream e(d / "edges.csv");
    e.precision(10);
    e << "u,v,length_m,highway,maxspeed_kph,oneway,geometry\n";
    edge(e, 1, 2, 111.2, 30.300, 78.0, 30.301, 78.0);
    edge(e, 2, 3, 6.0, 30.301, 78.0, 30.301054, 78.0);
    edge(e, 3, 4, 105.2, 30.301054, 78.0, 30.302, 78.0);
    edge(e, 4, 5, 96.0, 30.302, 78.0, 30.302, 78.001);
    return d;
}

static void writeJunctions(const fs::path& p) {
    std::ofstream(p) << "name,lat,lon,status,confidence,coord_source,evidence_source,evidence_url\n"
                        "Bee Chowk,30.301020,78.000010,documented_2024,high,table,\"UKMRC, CMP Annexure (2024)\",http://x\n"
                        "Dee Chowk,30.302000,78.001000,documented_2024,high,table,\"Source, with, commas\",http://x\n"
                        "Pending Chowk,,,reported_2025_pending,pending,,\"Live Hindustan, 2025\",http://y\n"
                        "Far Away Chowk,30.400000,78.100000,documented_2024,high,table,src,http://z\n";
}

static int edgeOf(const OsmNetwork& net, long long a, long long b) {
    for (int i = 0; i < net.graph->edge_count; i++)
        if (net.osmId[net.graph->edges[i].from] == a && net.osmId[net.graph->edges[i].to] == b) return i;
    return -1;
}

static void testLoadAndSnap(const OsmNetwork& net, const fs::path& csv) {
    JunctionIndex idx;
    std::string err;
    CHECK(loadJunctions(csv.string(), net, 50.0, idx, err));
    CHECK(idx.junctions.size() == 3);                      // pending row skipped
    CHECK(idx.skippedNoCoords == 1);
    CHECK(idx.unsnapped == 1);                             // Far Away Chowk
    CHECK(idx.junctions[0].name == "Bee Chowk" && idx.junctions[0].status == "documented_2024");
    CHECK(idx.junctions[0].nodes.size() == 2);             // B and B2
    CHECK(idx.junctions[1].nodes.size() == 1);             // D
    CHECK(idx.junctions[2].nodes.empty());
    CHECK(idx.junctionOf(net.indexOf.at(2)) == 0 && idx.junctionOf(net.indexOf.at(3)) == 0);
    CHECK(idx.junctionOf(net.indexOf.at(5)) == 1);
    CHECK(idx.junctionOf(net.indexOf.at(1)) == -1 && idx.junctionOf(net.indexOf.at(4)) == -1);
    CHECK(idx.junctionOf(-1) == -1 && idx.junctionOf(999) == -1);

    // A 5 m radius still catches B and B2 (2-4 m away) and D (exact).
    JunctionIndex tight;
    CHECK(loadJunctions(csv.string(), net, 5.0, tight, err));
    CHECK(tight.junctions[0].nodes.size() == 2 && tight.junctions[1].nodes.size() == 1);

    // Errors are reported, not ignored.
    JunctionIndex bad;
    CHECK(!loadJunctions((csv.parent_path() / "missing.csv").string(), net, 50.0, bad, err));
    std::ofstream(csv.parent_path() / "badhdr.csv") << "title,x,y\nA,1,2\n";
    CHECK(!loadJunctions((csv.parent_path() / "badhdr.csv").string(), net, 50.0, bad, err));
    std::ofstream(csv.parent_path() / "badnum.csv") << "name,lat,lon\nA,abc,78.0\n";
    CHECK(!loadJunctions((csv.parent_path() / "badnum.csv").string(), net, 50.0, bad, err));
}

static void testRouteCountAndEta(OsmNetwork& net, const fs::path& csv) {
    JunctionIndex idx;
    std::string err;
    CHECK(loadJunctions(csv.string(), net, 50.0, idx, err));
    int ab = edgeOf(net, 1, 2), bb2 = edgeOf(net, 2, 3), b2c = edgeOf(net, 3, 4), cd = edgeOf(net, 4, 5);
    std::vector<int> route = { ab, bb2, b2c, cd };

    // Bee Chowk is crossed at two nodes but counted once; then Dee Chowk.
    std::vector<int> on = junctionsOnRoute(net, idx, route);
    CHECK(on.size() == 2 && on[0] == 0 && on[1] == 1);
    // Starting AT a junction does not count it (only arrivals count).
    std::vector<int> fromB = junctionsOnRoute(net, idx, { b2c, cd });
    CHECK(fromB.size() == 1 && fromB[0] == 1);

    TrafficRules rules;
    TrafficState st(net);
    double ff = 0;
    for (int e : route) ff += net.graph->edges[e].duration_s;

    // Without junctions: the old behaviour (OSM signal at node 2 -> one 30 s wait).
    RouteSummary s0 = summarizeRoute(net, st, route, rules, 30.0);
    CHECK(s0.signals == 1 && s0.junctions == 0 && s0.junctionNames.empty());
    CHECK(near(s0.etaS, ff + 30.0, 1e-9));

    // With junctions: 2 junctions -> 2 waits; OSM signals no longer add waits.
    RouteSummary s1 = summarizeRoute(net, st, route, rules, 30.0, &idx);
    CHECK(s1.junctions == 2);
    CHECK(s1.junctionNames.size() == 2 && s1.junctionNames[0] == "Bee Chowk" && s1.junctionNames[1] == "Dee Chowk");
    CHECK(near(s1.freeFlowS, ff + 60.0, 1e-9) && near(s1.etaS, ff + 60.0, 1e-9));
    CHECK(s1.signals == 1);                                  // still reported, for information

    // Live TomTom data on A->B: Bee Chowk's wait is inside TomTom's speed, so only Dee Chowk adds 30 s.
    FlowSegment seg;
    CHECK(parseFlowSegment("{\"flowSegmentData\":{\"frc\":\"FRC2\",\"currentSpeed\":20,\"freeFlowSpeed\":40,"
                           "\"confidence\":0.9,\"roadClosure\":false,\"coordinates\":{\"coordinate\":["
                           "{\"latitude\":30.2995,\"longitude\":78.0},{\"latitude\":30.3009,\"longitude\":78.0}]}}}",
                           seg, err));
    CHECK(st.applySegment(seg, rules, { ab }) == 1);
    RouteSummary s2 = summarizeRoute(net, st, route, rules, 30.0, &idx);
    double live = 111.2 / (20 / 3.6);
    double rest = 0;
    for (int e : { bb2, b2c, cd }) rest += net.graph->edges[e].duration_s;
    CHECK(s2.junctions == 2);
    CHECK(near(s2.etaS, live + rest + 30.0, 1e-9));
    CHECK(near(s2.freeFlowS, ff + 60.0, 1e-9));             // free-flow ETA unchanged by live data
}

int main() {
    fs::path d = writeNetwork();
    OsmNetwork net;
    std::string err;
    if (!loadOsmNetwork(d.string(), net, err)) { std::cerr << "network: " << err << "\n"; return 1; }
    fs::path csv = d / "major_junctions.csv";
    writeJunctions(csv);
    testLoadAndSnap(net, csv);
    testRouteCountAndEta(net, csv);
    fs::remove_all(d);
    std::cout << "test_junctions: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
