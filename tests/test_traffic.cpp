// test_traffic.cpp — offline tests for the traffic module: parsing, rules,
// matching, TrafficState, the TomTom client's guardrails, and the ETA summary.
// TomTom is replaced by a fake fetch function: no network, no quota used.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../src/traffic/eta.hpp"
#include "../src/traffic/tomtom_client.hpp"

namespace fs = std::filesystem;
using namespace rl;

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #c "\n"; } } while (0)
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// A TomTom-style response for a northbound road along lon 78.0.
static std::string flowJson(double cur, double freeFlow, double conf, bool closure = false,
                            double lat0 = 30.2995, double lat1 = 30.3025, double lon = 78.0) {
    std::ostringstream o;
    o.precision(10);
    o << "{\"flowSegmentData\":{\"frc\":\"FRC2\",\"currentSpeed\":" << cur << ",\"freeFlowSpeed\":" << freeFlow
      << ",\"currentTravelTime\":60,\"freeFlowTravelTime\":30,\"confidence\":" << conf
      << ",\"roadClosure\":" << (closure ? "true" : "false") << ",\"coordinates\":{\"coordinate\":["
      << "{\"latitude\":" << lat0 << ",\"longitude\":" << lon << "},"
      << "{\"latitude\":" << (lat0 + lat1) / 2 << ",\"longitude\":" << lon << "},"
      << "{\"latitude\":" << lat1 << ",\"longitude\":" << lon << "}]},\"@version\":\"x\"}}";
    return o.str();
}

// ---------------------------------------------------------------- pure rules

static void testParse() {
    FlowSegment s;
    std::string err;
    CHECK(parseFlowSegment(flowJson(33, 42, 0.595945), s, err));
    CHECK(s.frc == "FRC2" && s.currentKph == 33 && s.freeFlowKph == 42 && near(s.confidence, 0.595945, 1e-9));
    CHECK(s.shape.size() == 3 && near(s.shape[0].lat, 30.2995, 1e-9) && !s.roadClosure);
    CHECK(parseFlowSegment(flowJson(0, 42, 1, true), s, err) && s.roadClosure);
    CHECK(!parseFlowSegment("not json", s, err));
    CHECK(!parseFlowSegment("{\"flowSegmentData\":{\"currentSpeed\":1}}", s, err));
    CHECK(!parseFlowSegment("{\"error\":\"x\"}", s, err));
}

static void testRules() {
    TrafficRules r;
    CHECK(levelForRatio(1.0, r) == TrafficLevel::Low);
    CHECK(levelForRatio(0.8, r) == TrafficLevel::Low);
    CHECK(levelForRatio(0.79, r) == TrafficLevel::Medium);     // Ballupur, 33/42
    CHECK(levelForRatio(0.62, r) == TrafficLevel::Medium);     // Survey Chowk, 23/37
    CHECK(levelForRatio(0.5, r) == TrafficLevel::Medium);
    CHECK(levelForRatio(0.49, r) == TrafficLevel::High);
    CHECK(levelForRatio(NAN, r) == TrafficLevel::Unknown);
    CHECK(sourceForConfidence(1.0, r) == DataSource::Live);
    CHECK(sourceForConfidence(0.61, r) == DataSource::Live);
    CHECK(sourceForConfidence(0.6, r) == DataSource::Historical);
    CHECK(sourceForConfidence(0.565093, r) == DataSource::Historical);
    CHECK(sourceForConfidence(0.5, r) == DataSource::Historical);
    CHECK(sourceForConfidence(0.49, r) == DataSource::FreeFlow);

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

    CHECK(near(bearingDeg({ 30.30, 78.0 }, { 30.31, 78.0 }), 0.0, 0.01));
    CHECK(near(bearingDeg({ 30.30, 78.0 }, { 30.30, 78.01 }), 90.0, 0.1));
    CHECK(near(angleDiffDeg(350, 10), 20, 1e-9) && near(angleDiffDeg(0, 180), 180, 1e-9));
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

static void testMatchingAndState() {
    OsmNetwork net;
    std::string err;
    CHECK(loadOsmNetwork(writeNetwork().string(), net, err));
    if (!net.graph) return;
    int ab = edgeOf(net, 1, 2), ba = edgeOf(net, 2, 1), bc = edgeOf(net, 2, 3), cd = edgeOf(net, 3, 4);
    CHECK(ab >= 0 && ba >= 0 && bc >= 0 && cd >= 0);

    FlowSegment seg;
    CHECK(parseFlowSegment(flowJson(20, 40, 0.9), seg, err));
    TrafficRules r;
    bool wrongWay = false;
    CHECK(edgeMatchesSegment(net.geometry[ab], seg, r, &wrongWay) && !wrongWay);
    CHECK(!edgeMatchesSegment(net.geometry[ba], seg, r, &wrongWay) && wrongWay);    // opposite direction
    CHECK(!edgeMatchesSegment(net.geometry[cd], seg, r, &wrongWay) && !wrongWay);   // side street, too far

    std::vector<int> all;
    for (int i = 0; i < net.graph->edge_count; i++) all.push_back(i);
    TrafficState st(net);
    CHECK(st.applySegment(seg, r, all) == 2);                   // A->B and B->C only
    CHECK(st.directionRejections() == 2);                       // B->A and C->B
    CHECK(st.edge(ab).source == DataSource::Live && near(st.edge(ab).ratio, 0.5, 1e-9));
    CHECK(near(st.edge(ab).durationS, 111.2 / (20 / 3.6), 1e-9));
    CHECK(near(net.graph->edges[ab].traffic_index, 0.5, 1e-9));
    CHECK(st.edge(ba).source == DataSource::FreeFlow && net.graph->edges[ba].traffic_index == 0.0);
    CHECK(near(st.edge(ba).durationS, net.graph->edges[ba].duration_s, 1e-12));

    // Costs use the live time: 20.016 s = 0.3336 min on A->B.
    Weights w{ 0.0, 1.0, 0.0, 0.0 };
    std::vector<double> c;
    st.computeCosts(w, c);
    CHECK(near(c[ab], (111.2 / (20 / 3.6)) / 60.0, 1e-9));

    // Low confidence is ignored; better data is not overwritten by worse data.
    FlowSegment weak;
    CHECK(parseFlowSegment(flowJson(5, 40, 0.4), weak, err));
    CHECK(st.applySegment(weak, r, all) == 0);
    FlowSegment hist;
    CHECK(parseFlowSegment(flowJson(30, 40, 0.55), hist, err));
    CHECK(st.applySegment(hist, r, all) == 0);                  // 0.9 already there
    st.reset();
    CHECK(st.applySegment(hist, r, all) == 2 && st.edge(bc).source == DataSource::Historical);

    // Closure: travel time stays free-flow, congestion is maximal.
    st.reset();
    FlowSegment closed;
    CHECK(parseFlowSegment(flowJson(0, 40, 1.0, true), closed, err));
    CHECK(st.applySegment(closed, r, all) == 2 && st.edge(ab).closed && net.graph->edges[ab].traffic_index == 1.0);

    // Sampling skips the residential edge; one sample 200 m along the primary road.
    std::vector<LatLon> smp = sampleRoute(net, { ab, bc, cd }, 400.0);
    CHECK(smp.size() == 1 && near(smp[0].lon, 78.0, 1e-9) && smp[0].lat > 30.301 && smp[0].lat < 30.302);
    CHECK(sampleRoute(net, { cd }, 400.0).empty());
    CHECK(tomtomCoversRoadClass("primary_link") && !tomtomCoversRoadClass("residential"));
}

// ---------------------------------------------------------------- TomTom client

struct Fake {
    int calls = 0;
    std::string lastUrl;
    HttpResponse next;
    double clock = 1000.0;
    double slept = 0.0;
    std::string today = "2026-10-18";
};

static TomTomClient makeClient(Fake& f, const std::string& usageFile, int cap = 2000, const std::string& key = "SECRETKEY") {
    TomTomOptions o;
    o.apiKey = key;
    o.dailyCap = cap;
    o.usageFile = usageFile;
    return TomTomClient(
        o, [&f](const std::string& url) { f.calls++; f.lastUrl = url; return f.next; },
        [&f] { return f.clock; }, [&f](double s) { f.slept += s; f.clock += s; }, [&f] { return f.today; });
}

static HttpResponse ok(const std::string& body) { HttpResponse r; r.status = 200; r.body = body; return r; }
static HttpResponse status(long s) { HttpResponse r; r.status = s; return r; }

static void testClient() {
    fs::path dir = fs::temp_directory_path() / "routelens_test_tomtom";
    fs::remove_all(dir);
    std::string usage = (dir / "usage.txt").string();

    // Fresh answer, then a cache hit for another point on the same road.
    Fake f;
    f.next = ok(flowJson(20, 40, 0.9));
    {
        TomTomClient c = makeClient(f, usage);
        FetchResult r1 = c.fetch({ 30.3005, 78.00002 });
        CHECK(r1.status == FetchStatus::Ok && f.calls == 1 && r1.snapM < 5);
        CHECK(f.lastUrl.find("point=30.300500,78.000020") != std::string::npos);
        CHECK(f.lastUrl.find("key=SECRETKEY") != std::string::npos);
        FetchResult r2 = c.fetch({ 30.3015, 78.0 });
        CHECK(r2.status == FetchStatus::CachedOk && f.calls == 1 && r2.segmentIndex == r1.segmentIndex);

        // A point 1.4 km away gets the same far road back: no coverage, cached as a miss.
        FetchResult r3 = c.fetch({ 30.310, 78.010 });
        CHECK(r3.status == FetchStatus::NoCoverage && f.calls == 2 && r3.snapM > 1000);
        CHECK(r3.message.find("SECRETKEY") == std::string::npos);
        CHECK(c.fetch({ 30.3101, 78.0101 }).status == FetchStatus::CachedNoCoverage && f.calls == 2);

        // Rate limit: requests 2 and 3 came at the same clock time, so the client waited.
        CHECK(f.slept >= 0.24);

        // After 5 minutes the cache expires and TomTom is asked again.
        f.clock += 301;
        CHECK(c.fetch({ 30.3015, 78.0 }).status == FetchStatus::Ok && f.calls == 3);
        CHECK(c.requestsToday() == 3 && c.requestsThisRun() == 3 && c.cacheHits() == 2);
    }

    // The daily count survives a restart, and resets on a new date.
    {
        TomTomClient c = makeClient(f, usage);
        CHECK(c.requestsToday() == 3);
        f.today = "2026-10-19";
        TomTomClient d = makeClient(f, usage);
        CHECK(d.requestsToday() == 0);
        f.today = "2026-10-18";
    }

    // Own daily cap: no request is made once it is reached.
    {
        Fake g;
        g.next = ok(flowJson(20, 40, 0.9));
        fs::remove(usage);
        TomTomClient c = makeClient(g, usage, 1);
        CHECK(c.fetch({ 30.3005, 78.0 }).status == FetchStatus::Ok && g.calls == 1);
        FetchResult r = c.fetch({ 30.400, 78.100 });
        CHECK(r.status == FetchStatus::DailyCapReached && g.calls == 1);
    }

    // Errors.
    auto once = [&](HttpResponse resp, FetchStatus expect, bool thenDisabled) {
        Fake h;
        h.next = resp;
        fs::remove(usage);
        TomTomClient c = makeClient(h, usage);
        FetchResult r = c.fetch({ 30.3005, 78.0 });
        CHECK(r.status == expect);
        CHECK(r.message.find("SECRETKEY") == std::string::npos);
        FetchResult again = c.fetch({ 30.400, 78.100 });
        if (thenDisabled) CHECK(again.status == FetchStatus::Disabled && h.calls == 1);
        else CHECK(h.calls == 2);
    };
    once(status(403), FetchStatus::AuthError, true);
    once(status(401), FetchStatus::AuthError, true);
    once(status(429), FetchStatus::QuotaExceeded, true);
    once(status(400), FetchStatus::NoCoverage, false);
    once(status(503), FetchStatus::Unavailable, false);
    once(ok("{\"oops\":1}"), FetchStatus::BadResponse, false);
    HttpResponse netErr;
    netErr.error = "Could not resolve host";
    once(netErr, FetchStatus::Unavailable, false);

    // Outage: after 3 network failures in a row, stop asking for this run.
    {
        Fake o;
        o.next = netErr;
        fs::remove(usage);
        TomTomClient c = makeClient(o, usage);
        CHECK(c.fetch({ 30.30, 78.00 }).status == FetchStatus::Unavailable);
        CHECK(c.fetch({ 30.31, 78.01 }).status == FetchStatus::Unavailable);
        CHECK(c.fetch({ 30.32, 78.02 }).status == FetchStatus::Unavailable && o.calls == 3);
        FetchResult r = c.fetch({ 30.33, 78.03 });
        CHECK(r.status == FetchStatus::Disabled && o.calls == 3);
        CHECK(r.message.find("unreachable 3 times") != std::string::npos);
    }
    // A success in between resets the failure count.
    {
        Fake o;
        fs::remove(usage);
        TomTomClient c = makeClient(o, usage);
        o.next = netErr;
        c.fetch({ 30.30, 78.00 });
        c.fetch({ 30.31, 78.01 });
        o.next = ok(flowJson(20, 40, 0.9));
        CHECK(c.fetch({ 30.3005, 78.0 }).status == FetchStatus::Ok);
        o.next = netErr;
        c.fetch({ 30.40, 78.10 });
        c.fetch({ 30.41, 78.11 });
        CHECK(c.fetch({ 30.42, 78.12 }).status == FetchStatus::Unavailable && o.calls == 6);
    }

    // No key: never calls TomTom.
    {
        Fake k;
        TomTomClient c = makeClient(k, usage, 2000, "");
        CHECK(c.fetch({ 30.3, 78.0 }).status == FetchStatus::Disabled && k.calls == 0);
    }
    fs::remove_all(dir);
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

    // With TomTom data on the primary road (live, half speed).
    fs::path dir = fs::temp_directory_path() / "routelens_test_tomtom2";
    Fake f;
    f.next = ok(flowJson(20, 40, 0.9));
    TomTomClient c = makeClient(f, (dir / "usage.txt").string());
    std::vector<FetchResult> res = updateRouteTraffic(net, st, c, route, r, 400.0);
    CHECK(res.size() == 1 && res[0].status == FetchStatus::Ok && f.calls == 1);
    RouteSummary s = summarizeRoute(net, st, route, r, 30.0);
    double live = 111.2 / (20 / 3.6);
    // Signal B is on a live edge (wait already in TomTom's speed); D is on a free-flow edge.
    CHECK(near(s.etaS, 2 * live + ffRes + 30, 1e-9));
    CHECK(near(s.liveShare, 222.4 / 318.4, 1e-9) && near(s.estimatedShare, 96.0 / 318.4, 1e-9));
    CHECK(near(s.meanRatio, 0.5, 1e-9) && s.level == TrafficLevel::Medium && !s.anyClosure);
    CHECK(coverageLabel(s) == "live on 70% of the route, typical on 0%, estimated on 30%");
    fs::remove_all(dir);
}

int main() {
    testParse();
    testRules();
    testMatchingAndState();
    testClient();
    testRouteSummary();
    std::cout << "test_traffic: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
