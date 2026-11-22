// test_scenarios.cpp — normal / rain / rush hour / incident scenarios on a small
// hand-made network, and their effect on costs and the route summary. Offline.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "../src/traffic/eta.hpp"
#include "../src/oop/scenario.hpp"

namespace fs = std::filesystem;
using namespace rl;

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #c "\n"; } } while (0)
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
#define CHECK_THROWS(expr) do { checks++; bool thrown_ = false; \
    try { (void)(expr); } catch (const std::invalid_argument&) { thrown_ = true; } \
    if (!thrown_) { failures++; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": expected invalid_argument: " #expr "\n"; } } while (0)
#define CHECK_NOTHROW(expr) do { checks++; try { (void)(expr); } catch (...) { failures++; \
    std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": unexpected throw: " #expr "\n"; } } while (0)

// A(1) -primary- B(2) -primary- C(3) -residential- D(4), all two-way. Signal at D.
static fs::path writeNetwork() {
    fs::path d = fs::temp_directory_path() / "routelens_test_scenarios";
    fs::create_directories(d);
    std::ofstream(d / "nodes.csv") << "osm_id,lat,lon,is_signal\n"
                                      "1,30.3000000,78.0000000,0\n"
                                      "2,30.3010000,78.0000000,0\n"
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

static void testParsing() {
    ScenarioKind s;
    CHECK(parseScenario("normal", s) && s == ScenarioKind::Normal);
    CHECK(parseScenario("rush_hour", s) && s == ScenarioKind::RushHour);
    CHECK(parseScenario("rain", s) && s == ScenarioKind::Rain);
    CHECK(parseScenario("incident", s) && s == ScenarioKind::Incident);
    CHECK(!parseScenario("Rain", s) && !parseScenario("", s) && !parseScenario("snow", s));
    CHECK(std::string(scenarioName(ScenarioKind::RushHour)) == "rush_hour");
    CHECK(scenarioNeedsLiveTraffic(ScenarioKind::Normal) && scenarioNeedsLiveTraffic(ScenarioKind::Incident));
    CHECK(!scenarioNeedsLiveTraffic(ScenarioKind::Rain) && !scenarioNeedsLiveTraffic(ScenarioKind::RushHour));
    CHECK(isMainRoad("primary") && isMainRoad("secondary_link") && isMainRoad("trunk"));
    CHECK(!isMainRoad("tertiary") && !isMainRoad("residential") && !isMainRoad(""));
}

static FlowSegment liveSegment() {   // TomTom-style: half speed, live, along A->B->C
    FlowSegment seg;
    std::string err;
    parseFlowSegment("{\"flowSegmentData\":{\"frc\":\"FRC2\",\"currentSpeed\":20,\"freeFlowSpeed\":40,"
                     "\"confidence\":0.9,\"roadClosure\":false,\"coordinates\":{\"coordinate\":["
                     "{\"latitude\":30.2995,\"longitude\":78.0},{\"latitude\":30.3025,\"longitude\":78.0}]}}}",
                     seg, err);
    return seg;
}

static void testScenarios(OsmNetwork& net) {
    int ab = edgeOf(net, 1, 2), bc = edgeOf(net, 2, 3), cd = edgeOf(net, 3, 4);
    std::vector<int> route = { ab, bc, cd };
    std::vector<int> all;
    for (int i = 0; i < net.graph->edge_count; i++) all.push_back(i);
    double ffP = net.graph->edges[ab].duration_s, ffR = net.graph->edges[cd].duration_s;
    TrafficRules rules;
    ScenarioSettings set;
    TrafficState st(net);

    // Normal: nothing changes.
    CHECK(applyScenario(st, ScenarioKind::Normal, set) == 0);
    CHECK(st.edge(ab).source == DataSource::FreeFlow && near(st.edge(ab).durationS, ffP, 1e-12));
    RouteSummary s0 = summarizeRoute(net, st, route, rules, 30.0);
    CHECK(near(s0.etaS, 2 * ffP + ffR + 30.0, 1e-9));   // old behaviour unchanged (signal at D)
    CHECK(coverageLabel(s0) == "no traffic data on this route: ETA is an estimate");

    // Live data on the primary road, then Rain: live data is ignored, every road x 0.88.
    CHECK(st.applySegment(liveSegment(), rules, all) == 2);
    CHECK(applyScenario(st, ScenarioKind::Rain, set) == net.graph->edge_count);
    CHECK(st.edge(ab).source == DataSource::Scenario && near(st.edge(ab).ratio, 0.88, 1e-12));
    CHECK(near(st.edge(ab).durationS, ffP / 0.88, 1e-9) && near(st.edge(cd).durationS, ffR / 0.88, 1e-9));
    CHECK(near(net.graph->edges[ab].traffic_index, 0.12, 1e-12));
    RouteSummary r = summarizeRoute(net, st, route, rules, 30.0);
    CHECK(near(r.etaS, (2 * ffP + ffR) / 0.88 + 30.0, 1e-9));   // waits added on scenario edges
    CHECK(near(r.scenarioShare, 1.0, 1e-12) && r.liveShare == 0.0);
    CHECK(near(r.meanRatio, 0.88, 1e-12) && r.level == TrafficLevel::Low);
    CHECK(coverageLabel(r) == "scenario estimate on 100% of the route");

    // Rush hour: primary x 0.65, residential x 0.85; level from the length-weighted ratio.
    CHECK(applyScenario(st, ScenarioKind::RushHour, set) == net.graph->edge_count);
    CHECK(near(st.edge(ab).durationS, ffP / 0.65, 1e-9) && near(st.edge(cd).durationS, ffR / 0.85, 1e-9));
    RouteSummary h = summarizeRoute(net, st, route, rules, 30.0);
    double ratio = (0.65 * 222.4 + 0.85 * 96.0) / 318.4;
    CHECK(near(h.meanRatio, ratio, 1e-9) && h.level == TrafficLevel::Medium);
    CHECK(near(h.etaS, 2 * ffP / 0.65 + ffR / 0.85 + 30.0, 1e-9));
    CHECK(h.etaS > r.etaS);

    // Incident (slowed) on top of live data: the live edge keeps its data and is slowed further.
    st.reset();
    CHECK(st.applySegment(liveSegment(), rules, all) == 2);
    double liveAb = st.edge(ab).durationS;
    Incident slow;
    slow.edges = { ab, -5, 9999 };   // invalid indices are ignored
    CHECK(applyScenario(st, ScenarioKind::Incident, set, &slow) == 1);
    CHECK(st.edge(ab).source == DataSource::Scenario && near(st.edge(ab).ratio, 0.5 * 0.30, 1e-12));
    CHECK(near(st.edge(ab).durationS, liveAb / 0.30, 1e-9) && !st.edge(ab).blocked);
    CHECK(near(net.graph->edges[ab].traffic_index, 0.85, 1e-12));
    CHECK(st.edge(bc).source == DataSource::Live);   // untouched
    RouteSummary i = summarizeRoute(net, st, route, rules, 30.0);
    CHECK(i.scenarioShare > 0 && i.liveShare > 0 && !i.anyBlocked);
    CHECK(coverageLabel(i) == "scenario estimate on 35% of the route, live/typical data on 35%");

    // Incident (blocked): huge routing cost, flagged in the summary.
    st.reset();
    Incident block;
    block.edges = { bc };
    block.blocked = true;
    CHECK(applyScenario(st, ScenarioKind::Incident, set, &block) == 1);
    CHECK(st.edge(bc).blocked && st.edge(bc).closed && net.graph->edges[bc].traffic_index == 1.0);
    Weights w{ 0.0, 1.0, 0.0, 0.0 };
    std::vector<double> c;
    st.computeCosts(w, c);
    CHECK(c[bc] >= 1e9 && c[ab] < 10.0);
    CHECK(summarizeRoute(net, st, route, rules, 30.0).anyBlocked);
    CHECK(!summarizeRoute(net, st, { ab }, rules, 30.0).anyBlocked);

    // Incident without a list changes nothing; reset clears the block.
    CHECK(applyScenario(st, ScenarioKind::Incident, set) == 0);
    st.reset();
    CHECK(!st.edge(bc).blocked && st.edge(bc).source == DataSource::FreeFlow);

    // Invalid settings: rejected, nothing changed.
    ScenarioSettings bad;
    bad.rainFactor = 0.0;
    CHECK(applyScenario(st, ScenarioKind::Rain, bad) == -1 && st.edge(ab).source == DataSource::FreeFlow);
    bad = ScenarioSettings{};
    bad.rushMainFactor = 1.5;
    CHECK(applyScenario(st, ScenarioKind::RushHour, bad) == -1);
    CHECK(std::string(sourceName(DataSource::Scenario)) == "scenario estimate");
}

// The class hierarchy: the same behaviour through base-class pointers (runtime
// polymorphism), constructor validation and the factory.
static void testHierarchy(OsmNetwork& net) {
    int ab = edgeOf(net, 1, 2), cd = edgeOf(net, 3, 4);
    ScenarioSettings set;
    TrafficState st(net);

    // One container of base pointers; each call dispatches to the derived class.
    Incident slow;
    slow.edges = { ab };
    std::vector<std::unique_ptr<Scenario>> all;
    all.push_back(std::make_unique<NormalScenario>());
    all.push_back(std::make_unique<RainScenario>());
    all.push_back(std::make_unique<RushHourScenario>());
    all.push_back(std::make_unique<IncidentScenario>(slow));
    const ScenarioKind kinds[] = { ScenarioKind::Normal, ScenarioKind::Rain, ScenarioKind::RushHour,
                                   ScenarioKind::Incident };
    const int edgeCount = net.graph->edge_count;
    const int expectedChanged[] = { 0, edgeCount, edgeCount, 1 };
    const bool needsData[] = { true, false, false, true };
    for (int i = 0; i < 4; i++) {
        st.reset();
        CHECK(all[i]->kind() == kinds[i]);
        CHECK(all[i]->apply(st) == expectedChanged[i]);
        CHECK(all[i]->needsTrafficData() == needsData[i]);
        CHECK(all[i]->needsTrafficData() == scenarioNeedsLiveTraffic(kinds[i]));
        CHECK(std::string(all[i]->name()) == scenarioName(kinds[i]));
    }

    // Same call, different effect: rain slows every edge by 0.88, rush hour treats
    // the primary edge and the residential edge differently.
    st.reset();
    RainScenario().apply(st);
    CHECK(near(st.edge(ab).ratio, 0.88, 1e-12) && near(st.edge(cd).ratio, 0.88, 1e-12));
    RushHourScenario().apply(st);
    CHECK(near(st.edge(ab).ratio, 0.65, 1e-12) && near(st.edge(cd).ratio, 0.85, 1e-12));
    RushHourScenario(0.5, 0.7).apply(st);
    CHECK(near(st.edge(ab).ratio, 0.5, 1e-12) && near(st.edge(cd).ratio, 0.7, 1e-12));

    // The class path and the wrapper function give identical results.
    st.reset();
    RainScenario(0.8).apply(st);
    double viaClass = st.edge(ab).durationS;
    st.reset();
    ScenarioSettings s80;
    s80.rainFactor = 0.8;
    applyScenario(st, ScenarioKind::Rain, s80);
    CHECK(near(viaClass, st.edge(ab).durationS, 1e-12));

    // An incident with a blocked road.
    Incident block;
    block.edges = { ab, 9999, -1 };   // out-of-range indices are ignored
    block.blocked = true;
    st.reset();
    CHECK(IncidentScenario(block).apply(st) == 1 && st.edge(ab).blocked);

    // Constructors reject invalid factors, so an existing object is always valid.
    CHECK_THROWS(RainScenario(0.0));
    CHECK_THROWS(RainScenario(1.01));
    CHECK_THROWS(RainScenario(-0.5));
    CHECK_THROWS(RushHourScenario(0.5, 0.0));
    CHECK_THROWS(RushHourScenario(2.0, 0.5));
    CHECK_THROWS(IncidentScenario(Incident{}, 0.0));
    CHECK_NOTHROW(RainScenario(1.0));

    // Factory: right class for each kind; invalid settings throw.
    for (int i = 0; i < 4; i++) CHECK(makeScenario(kinds[i], set)->kind() == kinds[i]);
    CHECK(dynamic_cast<RainScenario*>(makeScenario(ScenarioKind::Rain, set).get()) != nullptr);
    CHECK(dynamic_cast<RushHourScenario*>(makeScenario(ScenarioKind::Rain, set).get()) == nullptr);
    CHECK(makeScenario(ScenarioKind::Incident, set, &slow)->apply(st) == 1);
    ScenarioSettings bad;
    bad.incidentSlowFactor = 1.5;
    CHECK_THROWS(makeScenario(ScenarioKind::Normal, bad));

    // operator<< prints the scenario name through the base reference.
    std::ostringstream os;
    const Scenario& ref = *all[2];
    os << ref;
    CHECK(os.str() == "rush_hour");
}

int main() {
    testParsing();
    fs::path d = writeNetwork();
    OsmNetwork net;
    std::string err;
    if (!loadOsmNetwork(d.string(), net, err)) { std::cerr << "network: " << err << "\n"; return 1; }
    testScenarios(net);
    testHierarchy(net);
    fs::remove_all(d);
    std::cout << "test_scenarios: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
