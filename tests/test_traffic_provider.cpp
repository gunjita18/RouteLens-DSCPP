// test_traffic_provider.cpp — TrafficProvider hierarchy: FreeFlowProvider,
// SnapshotProvider (C hash table inside), the factory with its fallback, and
// applyProvider(). Offline, on a hand-made network and snapshot.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include "../src/oop/traffic_provider.hpp"

namespace fs = std::filesystem;
using namespace rl;

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #c "\n"; } } while (0)
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// A(1) -primary- B(2) -primary- C(3) -residential- D(4), all two-way.
// Data rows of edges.csv (these are the snapshot's edge ids):
//   0: 1->2   1: 2->1   2: 2->3   3: 3->2   4: 3->4   5: 4->3
static fs::path writeNetwork(const fs::path& d) {
    std::ofstream(d / "nodes.csv") << "osm_id,lat,lon,is_signal\n"
                                      "1,30.3000000,78.0000000,0\n"
                                      "2,30.3010000,78.0000000,0\n"
                                      "3,30.3020000,78.0000000,0\n"
                                      "4,30.3020000,78.0010000,0\n";
    std::ofstream(d / "edges.csv") << "u,v,length_m,highway,maxspeed_kph,oneway,geometry\n"
                                      "1,2,111.2,primary,,0,78.000000 30.300000;78.000000 30.301000\n"
                                      "2,1,111.2,primary,,0,78.000000 30.301000;78.000000 30.300000\n"
                                      "2,3,111.2,primary,,0,78.000000 30.301000;78.000000 30.302000\n"
                                      "3,2,111.2,primary,,0,78.000000 30.302000;78.000000 30.301000\n"
                                      "3,4,96.0,residential,,0,78.000000 30.302000;78.001000 30.302000\n"
                                      "4,3,96.0,residential,,0,78.001000 30.302000;78.000000 30.302000\n";
    return d;
}

static fs::path writeSnapshot(const fs::path& d) {
    fs::path p = d / "traffic.csv";
    std::ofstream(p) << "# fetched_at = 2026-11-10 09:30\n"
                        "edge_id,current_kph,freeflow_kph,confidence\n"
                        "0,20,40,0.9\n"      // 1->2: half speed
                        "2,0,40,0.8\n"       // 2->3: standstill, treated as closed
                        "4,10,20,0.7\n"      // 3->4: half speed
                        "99,30,40,0.9\n"     // no such edge in the network
                        "3,30,40,0.3\n";     // confidence below 0.5: rejected by the reader
    return p;
}

static int edgeOf(const OsmNetwork& net, long long a, long long b) {
    for (int i = 0; i < net.graph->edge_count; i++)
        if (net.osmId[net.graph->edges[i].from] == a && net.osmId[net.graph->edges[i].to] == b) return i;
    return -1;
}

int main() {
    fs::path d = fs::temp_directory_path() / "routelens_test_provider";
    fs::remove_all(d);
    fs::create_directories(d);
    writeNetwork(d);
    fs::path snap = writeSnapshot(d);

    OsmNetwork net;
    std::string err;
    if (!loadOsmNetwork(d.string(), net, err)) { std::cerr << "network: " << err << "\n"; return 1; }
    int ab = edgeOf(net, 1, 2), ba = edgeOf(net, 2, 1), bc = edgeOf(net, 2, 3);
    int cb = edgeOf(net, 3, 2), cd = edgeOf(net, 3, 4);
    CHECK(ab == 0 && bc == 2 && cd == 4);       // graph index = data row here: nothing was dropped

    // ---- FreeFlowProvider: no data anywhere
    FreeFlowProvider free;
    CHECK(!free.hasData(ab) && free.trafficIndex(ab) == 0.0 && free.currentKph(ab) == 0.0);
    CHECK(free.edgesWithData() == 0 && free.fetchedAt().empty());
    CHECK(free.description() == "estimated (free-flow speeds)");

    // ---- SnapshotProvider
    SnapshotProvider sp(net, snap.string());
    CHECK(sp.rowsRead() == 5 && sp.rowsSkipped() == 1);
    CHECK(sp.edgesWithData() == 3);
    CHECK(sp.unmatchedRows() == 1);             // edge_id 99 is not in the network
    CHECK(sp.fetchedAt() == "2026-11-10 09:30");
    CHECK(sp.description() == "snapshot 2026-11-10 09:30");

    CHECK(sp.hasData(ab) && near(sp.trafficIndex(ab), 0.5, 1e-12));
    CHECK(near(sp.currentKph(ab), 20.0, 1e-12) && near(sp.confidence(ab), 0.9, 1e-12));
    CHECK(sp.hasData(bc) && near(sp.trafficIndex(bc), 1.0, 1e-12) && sp.currentKph(bc) == 0.0);
    CHECK(sp.hasData(cd) && near(sp.trafficIndex(cd), 0.5, 1e-12));
    CHECK(!sp.hasData(ba) && sp.trafficIndex(ba) == 0.0 && sp.confidence(ba) == 0.0);
    CHECK(!sp.hasData(cb));                     // its row had confidence 0.3
    CHECK(!sp.hasData(-1) && !sp.hasData(1000) && sp.trafficIndex(1000) == 0.0);

    // ---- one interface, two behaviours (runtime polymorphism)
    std::vector<std::unique_ptr<TrafficProvider>> providers;
    providers.push_back(std::make_unique<FreeFlowProvider>());
    providers.push_back(std::make_unique<SnapshotProvider>(net, snap.string()));
    CHECK(!providers[0]->hasData(ab) && providers[1]->hasData(ab));
    CHECK(providers[0]->trafficIndex(ab) == 0.0 && providers[1]->trafficIndex(ab) > 0.0);
    CHECK(providers[0]->description() != providers[1]->description());

    // ---- LoadError: custom exception, caught as its own type and as std::runtime_error
    bool caught = false;
    try { SnapshotProvider missing(net, (d / "nope.csv").string()); }
    catch (const LoadError& e) { caught = std::string(e.what()).find("nope.csv") != std::string::npos; }
    CHECK(caught);
    bool caughtBase = false;
    try { SnapshotProvider missing(net, (d / "nope.csv").string()); }
    catch (const std::runtime_error&) { caughtBase = true; }
    CHECK(caughtBase);

    // ---- factory: snapshot when readable, free-flow fallback with a warning otherwise
    std::string warning;
    auto good = makeTrafficProvider(net, snap.string(), &warning);
    CHECK(dynamic_cast<SnapshotProvider*>(good.get()) != nullptr && warning.empty());
    auto fallback = makeTrafficProvider(net, (d / "nope.csv").string(), &warning);
    CHECK(dynamic_cast<FreeFlowProvider*>(fallback.get()) != nullptr);
    CHECK(warning.find("free-flow") != std::string::npos && warning.find("nope.csv") != std::string::npos);
    CHECK(makeTrafficProvider(net, (d / "nope.csv").string()) != nullptr);   // warning pointer is optional

    // ---- applyProvider: writes the data into the traffic state
    TrafficState st(net);
    double ffBC = net.graph->edges[bc].duration_s;
    CHECK(applyProvider(st, FreeFlowProvider()) == 0);
    CHECK(st.edge(ab).source == DataSource::FreeFlow);

    CHECK(applyProvider(st, sp) == 3);
    CHECK(st.edge(ab).source == DataSource::Historical && near(st.edge(ab).ratio, 0.5, 1e-12));
    CHECK(near(st.edge(ab).durationS, 111.2 / (20.0 / 3.6), 1e-9));        // length / measured speed
    CHECK(near(net.graph->edges[ab].traffic_index, 0.5, 1e-12));
    CHECK(near(st.edge(cd).durationS, 96.0 / (10.0 / 3.6), 1e-9));
    CHECK(st.edge(bc).closed && near(st.edge(bc).durationS, ffBC, 1e-12));  // 0 km/h: closed, free-flow time kept
    CHECK(near(net.graph->edges[bc].traffic_index, 1.0, 1e-12));
    CHECK(st.edge(ba).source == DataSource::FreeFlow && net.graph->edges[ba].traffic_index == 0.0);   // untouched

    // reset() returns to free flow
    st.reset();
    CHECK(st.edge(ab).source == DataSource::FreeFlow && net.graph->edges[ab].traffic_index == 0.0);

    fs::remove_all(d);
    std::cout << "test_traffic_provider: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
