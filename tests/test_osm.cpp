// test_osm.cpp — speed rules, the OSM id index, BFS graph cleaning and the OSM
// CSV loader, offline against small hand-made inputs (plus the real network
// when data/osm is present).
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
           "3,4,50.00,service,,0,78.001000 30.301000;78.001000 30.302000\n"
           "2,3,200.00,primary,50,0,78.000000 30.301000;78.000500 30.301050;78.001000 30.301000\n"
           "3,2,200.00,primary,50,0,78.001000 30.301000;78.000000 30.301000\n"
           "3,1,300.00,secondary,,1,78.001000 30.301000;78.000000 30.300000\n"
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
    CHECK(s.weakComponents == 2);     // {1,2,3,4} (4 is linked, one-way) and {5} (self-loop only)
    CHECK(s.keptEdges == 5);
    CHECK(s.droppedEdges == 1);       // 3->4 leaves the SCC
    CHECK(s.signalNodes == 1);
    CHECK(s.edgesWithMaxspeed == 2);
    CHECK(net.graph->node_count == 3 && net.graph->edge_count == 5);
    CHECK(net.indexOf.find(4) == -1 && net.indexOf.find(5) == -1);
    CHECK(net.indexOf.size() == 3);

    // Hash table and reverse mapping agree.
    for (size_t i = 0; i < net.osmId.size(); i++) CHECK(net.indexOf.find(net.osmId[i]) == (int)i);

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

    // csvRow keeps the original data row of every kept edge. Row 2 (3->4) is
    // dropped by cleaning, so the kept rows are 0, 1, 3, 4, 5 (a gap at 2);
    // rows 6-8 were rejected while reading.
    const int expectedRows[] = { 0, 1, 3, 4, 5 };
    CHECK(net.csvRow.size() == 5);
    for (size_t i = 0; i < net.csvRow.size() && i < 5; i++) CHECK(net.csvRow[i] == expectedRows[i]);

    // Bad header is rejected with a clear error.
    std::ofstream(d / "nodes.csv") << "id,lat,lon\n1,30,78\n";
    OsmNetwork bad;
    CHECK(!loadOsmNetwork(d.string(), bad, err) && err.find("header") != std::string::npos);
    CHECK(!loadOsmNetwork((d / "missing").string(), bad, err));
}

static void testIdIndex() {
    IdIndex idx;
    CHECK(idx.size() == 0 && idx.find(42) == -1);
    for (int i = 0; i < 50000; i++) CHECK(idx.put(2000000000LL + 3LL * i, i));
    CHECK(idx.size() == 50000);
    bool all = true;
    for (int i = 0; i < 50000; i++) if (idx.find(2000000000LL + 3LL * i) != i) { all = false; break; }
    CHECK(all);
    CHECK(idx.find(2000000001LL) == -1);

    IdIndex moved(std::move(idx));                       // ownership transfers
    CHECK(moved.size() == 50000 && moved.find(2000000000LL) == 0);
    CHECK(idx.find(2000000000LL) == -1);                 // moved-from is empty, still safe
    IdIndex assigned;
    assigned = std::move(moved);
    CHECK(assigned.size() == 50000 && moved.find(2000000000LL) == -1);
}

// The fixture's nodes are far from the default centre (Clock Tower), and the
// nearest node (5) is not in the main component, so cleaning must fall back
// to the next-nearest nodes and still keep the 3-node triangle.
static void testCleaningCentre() {
    fs::path d = writeFixture();
    std::string err;

    OsmNetwork byDefault;
    CHECK(loadOsmNetwork(d.string(), byDefault, err));
    CHECK(byDefault.stats.keptNodes == 3);

    // Explicit centre right next to node 1: the first try already succeeds.
    OsmLoadOptions opt;
    opt.centre = { 30.3000001, 78.0000001 };
    OsmNetwork nearOne;
    CHECK(loadOsmNetwork(d.string(), nearOne, err, opt));
    CHECK(nearOne.stats.keptNodes == 3 && nearOne.stats.bfsCentre == 0);
    CHECK(nearOne.indexOf.find(1) >= 0 && nearOne.indexOf.find(4) == -1);

    // Explicit centre next to the dead-end node 4: the main component is still found.
    opt.centre = { 30.3020, 78.0010 };
    OsmNetwork nearFour;
    CHECK(loadOsmNetwork(d.string(), nearFour, err, opt));
    CHECK(nearFour.stats.keptNodes == 3);
}

// Real Dehradun network (not in git; skipped if data/osm is absent).
// meta.txt records NetworkX's largest strongly connected component.
static void testRealNetwork() {
    std::ifstream probe("data/osm/nodes.csv");
    if (!probe) { std::cout << "note: data/osm not found, real-network test skipped\n"; return; }
    OsmNetwork net;
    std::string err;
    CHECK(loadOsmNetwork("data/osm", net, err));
    CHECK(net.stats.rawNodes == 32110);
    CHECK(net.stats.keptNodes == 31802);                 // NetworkX result in meta.txt
    CHECK(net.stats.weakComponents == 72);               // NetworkX weakly connected components
    CHECK(net.graph && net.graph->node_count == 31802);
    CHECK(net.indexOf.size() == 31802);
    for (size_t i = 0; i < net.osmId.size(); i += 97) CHECK(net.indexOf.find(net.osmId[i]) == (int)i);
}

int main() {
    testSpeeds();
    testIdIndex();
    testLoader();
    testCleaningCentre();
    testRealNetwork();
    std::cout << "test_osm: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
