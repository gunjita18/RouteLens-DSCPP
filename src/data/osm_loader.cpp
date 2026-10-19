#include "osm_loader.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

namespace rl {

// ---------------------------------------------------------------- speeds

double defaultSpeedKph(const std::string& highway) {
    // Wider, higher-class roads get higher speeds; narrow local streets lower.
    static const std::unordered_map<std::string, double> kSpeeds = {
        { "motorway", 60 },     { "motorway_link", 40 },
        { "trunk", 50 },        { "trunk_link", 35 },
        { "primary", 40 },      { "primary_link", 30 },
        { "secondary", 35 },    { "secondary_link", 25 },
        { "tertiary", 30 },     { "tertiary_link", 25 },
        { "unclassified", 25 }, { "road", 20 },
        { "residential", 20 },  { "service", 15 },
        { "living_street", 10 },
    };
    auto it = kSpeeds.find(highway);
    return it == kSpeeds.end() ? 20.0 : it->second;   // unknown class: treat as residential
}

static bool parseDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    try {
        size_t pos = 0;
        out = std::stod(s, &pos);
        return pos == s.size();
    } catch (...) {
        return false;
    }
}

static bool parseLongLong(const std::string& s, long long& out) {
    if (s.empty()) return false;
    try {
        size_t pos = 0;
        out = std::stoll(s, &pos);
        return pos == s.size();
    } catch (...) {
        return false;
    }
}

double edgeSpeedKph(const std::string& highway, const std::string& maxspeedKph) {
    double v = 0.0;
    if (parseDouble(maxspeedKph, v) && v > 0.0) return v;
    return defaultSpeedKph(highway);
}

// ---------------------------------------------------------------- Tarjan

std::vector<int> tarjanScc(int nodeCount, const std::vector<std::vector<int>>& adj,
                           int& componentCount) {
    std::vector<int> index(nodeCount, -1), low(nodeCount, 0), comp(nodeCount, -1);
    std::vector<char> onStack(nodeCount, 0);
    std::vector<int> stack;
    stack.reserve(nodeCount);

    struct Frame {
        int v;
        size_t next;   // next neighbour of v to look at
    };
    std::vector<Frame> call;   // replaces the recursion stack
    int nextIndex = 0;
    componentCount = 0;

    for (int s = 0; s < nodeCount; s++) {
        if (index[s] != -1) continue;
        index[s] = low[s] = nextIndex++;
        stack.push_back(s);
        onStack[s] = 1;
        call.push_back({ s, 0 });

        while (!call.empty()) {
            int v = call.back().v;
            if (call.back().next < adj[v].size()) {
                int w = adj[v][call.back().next++];
                if (index[w] == -1) {             // tree edge: "recurse" into w
                    index[w] = low[w] = nextIndex++;
                    stack.push_back(w);
                    onStack[w] = 1;
                    call.push_back({ w, 0 });
                } else if (onStack[w]) {          // back edge into the current SCC
                    low[v] = std::min(low[v], index[w]);
                }
            } else {                              // all neighbours done: "return" from v
                if (low[v] == index[v]) {         // v is the root of an SCC
                    int w;
                    do {
                        w = stack.back();
                        stack.pop_back();
                        onStack[w] = 0;
                        comp[w] = componentCount;
                    } while (w != v);
                    componentCount++;
                }
                call.pop_back();
                if (!call.empty()) {
                    int parent = call.back().v;
                    low[parent] = std::min(low[parent], low[v]);
                }
            }
        }
    }
    return comp;
}

// ---------------------------------------------------------------- CSV

// Splits one CSV line; supports quoted fields with "" escapes.
static std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> fields;
    std::string cur;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; i++; }
            else if (c == '"') quoted = false;
            else cur += c;
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            fields.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    fields.push_back(cur);
    return fields;
}

// "lon lat;lon lat;..." -> points. Returns false on any malformed point.
static bool parseGeometry(const std::string& s, std::vector<LatLon>& out) {
    out.clear();
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(';', start);
        if (end == std::string::npos) end = s.size();
        std::string pt = s.substr(start, end - start);
        size_t sp = pt.find(' ');
        double lon, lat;
        if (sp == std::string::npos || !parseDouble(pt.substr(0, sp), lon) ||
            !parseDouble(pt.substr(sp + 1), lat))
            return false;
        out.push_back({ lat, lon });
        start = end + 1;
    }
    return out.size() >= 2;
}

// ---------------------------------------------------------------- OsmNetwork

OsmNetwork::~OsmNetwork() { graph_free(graph); }

OsmNetwork::OsmNetwork(OsmNetwork&& o) noexcept { *this = std::move(o); }

OsmNetwork& OsmNetwork::operator=(OsmNetwork&& o) noexcept {
    if (this != &o) {
        graph_free(graph);
        graph = o.graph;
        o.graph = nullptr;
        osmId = std::move(o.osmId);
        pos = std::move(o.pos);
        isSignal = std::move(o.isSignal);
        indexOf = std::move(o.indexOf);
        geometry = std::move(o.geometry);
        roadClass = std::move(o.roadClass);
        stats = o.stats;
    }
    return *this;
}

// ---------------------------------------------------------------- loader

namespace {
struct RawEdge {
    int u, v;
    double lengthM;
    std::string highway, maxspeed;
    std::vector<LatLon> geometry;
};
}  // namespace

bool loadOsmNetwork(const std::string& dir, OsmNetwork& out, std::string& err) {
    OsmLoadStats st;

    // ---- 1. nodes: OSM id -> raw index (hash map)
    std::ifstream nin(dir + "/nodes.csv");
    if (!nin) { err = "cannot open " + dir + "/nodes.csv (run preprocess/export_osm.py)"; return false; }
    std::string line;
    if (!std::getline(nin, line) || splitCsv(line) != std::vector<std::string>{ "osm_id", "lat", "lon", "is_signal" }) {
        err = dir + "/nodes.csv: unexpected header";
        return false;
    }
    std::vector<long long> rawId;
    std::vector<LatLon> rawPos;
    std::vector<char> rawSignal;
    std::unordered_map<long long, int> rawIndex;
    int lineNo = 1;
    while (std::getline(nin, line)) {
        lineNo++;
        if (line.empty() || line == "\r") continue;
        auto f = splitCsv(line);
        long long id;
        double lat, lon;
        if (f.size() != 4 || !parseLongLong(f[0], id) || !parseDouble(f[1], lat) || !parseDouble(f[2], lon) ||
            (f[3] != "0" && f[3] != "1")) {
            err = dir + "/nodes.csv:" + std::to_string(lineNo) + ": malformed row";
            return false;
        }
        if (!rawIndex.emplace(id, (int)rawId.size()).second) {
            err = dir + "/nodes.csv:" + std::to_string(lineNo) + ": duplicate osm_id " + f[0];
            return false;
        }
        rawId.push_back(id);
        rawPos.push_back({ lat, lon });
        rawSignal.push_back(f[3] == "1");
    }
    st.rawNodes = (int)rawId.size();
    if (st.rawNodes == 0) { err = dir + "/nodes.csv has no nodes"; return false; }

    // ---- 2. edges
    std::ifstream ein(dir + "/edges.csv");
    if (!ein) { err = "cannot open " + dir + "/edges.csv"; return false; }
    if (!std::getline(ein, line) ||
        splitCsv(line) != std::vector<std::string>{ "u", "v", "length_m", "highway", "maxspeed_kph", "oneway", "geometry" }) {
        err = dir + "/edges.csv: unexpected header";
        return false;
    }
    std::vector<RawEdge> raw;
    std::vector<std::vector<int>> adj(st.rawNodes);
    while (std::getline(ein, line)) {
        if (line.empty() || line == "\r") continue;
        st.rawEdges++;
        auto f = splitCsv(line);
        long long uid, vid;
        RawEdge e;
        if (f.size() != 7 || !parseLongLong(f[0], uid) || !parseLongLong(f[1], vid) ||
            !parseDouble(f[2], e.lengthM) || e.lengthM < 0 || !parseGeometry(f[6], e.geometry)) {
            st.badRows++;
            continue;
        }
        auto iu = rawIndex.find(uid), iv = rawIndex.find(vid);
        if (iu == rawIndex.end() || iv == rawIndex.end()) { st.badRows++; continue; }
        if (uid == vid) { st.selfLoops++; continue; }
        e.u = iu->second;
        e.v = iv->second;
        e.highway = f[3];
        e.maxspeed = f[4];
        adj[e.u].push_back(e.v);
        raw.push_back(std::move(e));
    }

    // ---- 3. largest strongly connected component
    std::vector<int> comp = tarjanScc(st.rawNodes, adj, st.sccCount);
    std::vector<int> compSize(st.sccCount, 0);
    for (int c : comp) compSize[c]++;
    int best = (int)(std::max_element(compSize.begin(), compSize.end()) - compSize.begin());

    // ---- 4. re-index the kept nodes 0..n-1
    OsmNetwork net;
    std::vector<int> newIndex(st.rawNodes, -1);
    for (int i = 0; i < st.rawNodes; i++) {
        if (comp[i] != best) continue;
        newIndex[i] = (int)net.osmId.size();
        net.indexOf[rawId[i]] = newIndex[i];
        net.osmId.push_back(rawId[i]);
        net.pos.push_back(rawPos[i]);
        net.isSignal.push_back(rawSignal[i]);
        if (rawSignal[i]) st.signalNodes++;
    }
    st.keptNodes = (int)net.osmId.size();

    // ---- 5. build the routing graph
    net.graph = graph_create(st.keptNodes);
    if (!net.graph) { err = "out of memory creating graph"; return false; }
    for (RawEdge& e : raw) {
        int a = newIndex[e.u], b = newIndex[e.v];
        if (a < 0 || b < 0) { st.droppedEdges++; continue; }
        double kph = edgeSpeedKph(e.highway, e.maxspeed);
        double durationS = e.lengthM / (kph / 3.6);
        // A signal is charged to the edge that ENDS at it, so each signal on a
        // route is counted once, when the driver arrives at it.
        long long sig = rawId[e.v];
        bool hasSignal = net.isSignal[b] != 0;
        int idx = graph_add_edge(net.graph, a, b, e.lengthM, durationS, 0.0,
                                 hasSignal ? &sig : nullptr, hasSignal ? 1 : 0);
        if (idx != (int)net.geometry.size()) {
            err = "graph_add_edge failed for OSM edge " + std::to_string(rawId[e.u]) + " -> " +
                  std::to_string(rawId[e.v]);
            return false;
        }
        if (!e.maxspeed.empty()) st.edgesWithMaxspeed++;
        net.geometry.push_back(std::move(e.geometry));
        net.roadClass.push_back(e.highway);
    }
    st.keptEdges = (int)net.geometry.size();

    net.stats = st;
    out = std::move(net);
    return true;
}

}  // namespace rl
