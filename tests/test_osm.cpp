// test_osm.cpp — iterative Tarjan SCC, speed rules and the OSM CSV loader,
// all offline against small hand-made inputs.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

#include "../src/data/osm_loader.hpp"

namespace fs = std::filesystem;
using namespace rl;

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #c "\n"; } } while (0)
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static void testTarjanSmall() {
    // 0->1->2->0 is one SCC; 2->3; 3<->4 is another; 5 is alone.
    std::vector<std::vector<int>> adj = { { 1 }, { 2 }, { 0, 3 }, { 4 }, { 3 }, {} };
    int count = 0;
    auto comp = tarjanScc(6, adj, count);
    CHECK(count == 3);
    CHECK(comp[0] == comp[1] && comp[1] == comp[2]);
    CHECK(comp[3] == comp[4]);
    CHECK(comp[0] != comp[3] && comp[5] != comp[0] && comp[5] != comp[3]);

    // A one-way chain: every node is its own SCC.
    std::vector<std::vector<int>> chain = { { 1 }, { 2 }, { 3 }, {} };
    auto c2 = tarjanScc(4, chain, count);
    CHECK(count == 4);
    CHECK(std::set<int>(c2.begin(), c2.end()).size() == 4);

    // Empty graph.
    auto c3 = tarjanScc(0, {}, count);
    CHECK(count == 0 && c3.empty());
}

static void testTarjanDeep() {
    // A 300,000-node cycle would overflow the stack with recursive Tarjan.
    const int n = 300000;
    std::vector<std::vector<int>> adj(n);
    for (int i = 0; i < n; i++) adj[i].push_back((i + 1) % n);
    int count = 0;
    auto comp = tarjanScc(n, adj, count);
    CHECK(count == 1);
    CHECK(comp[0] == comp[n - 1]);
}

static void testSpeeds() {
    CHECK(defaultSpeedKph("primary") == 40);
    CHECK(defaultSpeedKph("residential") == 20);
    CHECK(defaultSpeedKph("living_street") == 10);
    CHECK(defaultSpeedKph("no_such_class") == 20);
    CHECK(defaultSpeedKph("residential") < defaultSpeedKph("secondary"));
    CHECK(edgeSpeedKph("residential", "50") == 50);     // tag wins
    CHECK(edgeSpeedKph("residential", "") == 20);       // missing tag
    CHECK(edgeSpeedKph("primary", "abc") == 40);        // unparsable tag
    CHECK(edgeSpeedKph("primary", "0") == 40);          // non-positive tag
}

static fs::path writeFixture() {
    fs::path d = fs::temp_directory_path() / "routelens_test_osm";
    fs::create_directories(d);
    // Nodes 1,2,3 form a two-way triangle (the largest SCC); 3 is a signal.
    // 4 is a dead end (only 3->4), 5 has a self-loop.
    std::ofstream(d / "nodes.csv") << "osm_id,lat,lon,is_signal\n"
                                      "1,30.3000000,78.0000000,0\n"
                                      "2,30.3010000,78.0000000,0\n"
                                      "3,30.3010000,78.0010000,1\n"
                                      "4,30.3020000,78.0010000,0\n"
                                      "5,30.3030000,78.0030000,0\n";
    std::ofstream(d / "edges.csv")
        << "u,v,length_m,highway,maxspeed_kph,oneway,geometry\n"
           "1,2,100.00,residential,,0,78.000000 30.300000;78.000000 30.301000\n"
           "2,1,100.00,residential,,0,78.000000 30.301000;78.000000 30.300000\n"
           "2,3,200.00,primary,50,0,78.000000 30.301000;78.000500 30.301050;78.001000 30.301000\n"
           "3,2,200.00,primary,50,0,78.001000 30.301000;78.000000 30.301000\n"
           "3,1,300.00,secondary,,1,78.001000 30.301000;78.000000 30.300000\n"
           "3,4,50.00,service,,0,78.001000 30.301000;78.001000 30.302000\n"
           "5,5,80.00,residential,,0,78.003000 30.303000;78.003100 30.303100\n"
           "1,999,10.00,residential,,0,78.000000 30.300000;78.000100 30.300100\n"
           "1,2,not_a_number,residential,,0,78.000000 30.300000;78.000000 30.301000\n";
    return d;
}

static void testLoader() {
    fs::path d = writeFixture();
    OsmNetwork net;
    std::string err;
    CHECK(loadOsmNetwork(d.string(), net, err));
    if (!net.graph) { std::cerr << "loader error: " << err << "\n"; failures++; return; }

    const OsmLoadStats& s = net.stats;
    CHECK(s.rawNodes == 5);
    CHECK(s.rawEdges == 9);
    CHECK(s.selfLoops == 1);
    CHECK(s.badRows == 2);            // unknown node 999 + bad length
    CHECK(s.keptNodes == 3);          // triangle only
    CHECK(s.keptEdges == 5);
    CHECK(s.droppedEdges == 1);       // 3->4 leaves the SCC
    CHECK(s.signalNodes == 1);
    CHECK(s.edgesWithMaxspeed == 2);
    CHECK(net.graph->node_count == 3 && net.graph->edge_count == 5);
    CHECK(net.indexOf.count(4) == 0 && net.indexOf.count(5) == 0);

    // Hash map and reverse mapping agree.
    for (size_t i = 0; i < net.osmId.size(); i++) CHECK(net.indexOf.at(net.osmId[i]) == (int)i);

    // Durations: 100 m residential at 20 km/h = 18 s; 200 m tagged 50 km/h = 14.4 s.
    bool saw2to3 = false, saw1to2 = false;
    for (int i = 0; i < net.graph->edge_count; i++) {
        const Edge& e = net.graph->edges[i];
        long long a = net.osmId[e.from], b = net.osmId[e.to];
        if (a == 1 && b == 2) { saw1to2 = true; CHECK(near(e.duration_s, 18.0, 1e-9)); }
        if (a == 2 && b == 3) {
            saw2to3 = true;
            CHECK(near(e.duration_s, 14.4, 1e-9));
            CHECK(e.signal_count == 1 && e.signal_ids[0] == 3);   // charged on arrival
            CHECK(net.geometry[i].size() == 3);
            CHECK(near(net.geometry[i][1].lat, 30.30105, 1e-9) && near(net.geometry[i][1].lon, 78.0005, 1e-9));
        }
        if (a == 3) CHECK(e.signal_count == 0);   // leaving a signal is not charged again
        CHECK(e.traffic_index == 0.0);
    }
    CHECK(saw1to2 && saw2to3);

    // Bad header is rejected with a clear error.
    std::ofstream(d / "nodes.csv") << "id,lat,lon\n1,30,78\n";
    OsmNetwork bad;
    CHECK(!loadOsmNetwork(d.string(), bad, err) && err.find("header") != std::string::npos);
    CHECK(!loadOsmNetwork((d / "missing").string(), bad, err));
}

int main() {
    testTarjanSmall();
    testTarjanDeep();
    testSpeeds();
    testLoader();
    std::cout << "test_osm: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
