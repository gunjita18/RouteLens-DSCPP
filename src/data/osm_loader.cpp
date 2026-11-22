#include "osm_loader.hpp"

#include <algorithm>
#include <fstream>
#include <new>
#include <unordered_map>
#include <utility>

#include "../core/bfs.h"

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

// ---------------------------------------------------------------- IdIndex

namespace {
constexpr int kInitialIdBuckets = 1024;
}

IdIndex::IdIndex() {
    if (ht_init(&table_, kInitialIdBuckets) != 0) throw std::bad_alloc();
}

IdIndex::~IdIndex() { ht_free(&table_); }

IdIndex::IdIndex(IdIndex&& o) noexcept : table_(o.table_) {
    o.table_ = HashTable{ nullptr, 0, 0, 0 };     // moved-from: empty, safe to destroy
}

IdIndex& IdIndex::operator=(IdIndex&& o) noexcept {
    if (this != &o) {
        ht_free(&table_);
        table_ = o.table_;
        o.table_ = HashTable{ nullptr, 0, 0, 0 };
    }
    return *this;
}

bool IdIndex::put(long long osmId, int index) {
    return table_.buckets != nullptr && ht_put(&table_, osmId, index) == 0;
}

int IdIndex::find(long long osmId) const {
    int index = -1;
    if (table_.buckets != nullptr && ht_get(&table_, osmId, &index)) return index;
    return -1;
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
        csvRow = std::move(o.csvRow);
        stats = o.stats;
    }
    return *this;
}

// ---------------------------------------------------------------- loader

namespace {
struct RawEdge {
    int u, v;
    int csvRow = -1;                // 0-based data row in edges.csv (the stable edge id)
    double lengthM;
    std::string highway, maxspeed;
    std::vector<LatLon> geometry;
};
}  // namespace

bool loadOsmNetwork(const std::string& dir, OsmNetwork& out, std::string& err,
                    const OsmLoadOptions& options) {
    OsmLoadStats st;

    // ---- 1. nodes: OSM id -> raw index (C hash table)
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
    IdIndex rawIndex;
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
        if (rawIndex.find(id) != -1) {
            err = dir + "/nodes.csv:" + std::to_string(lineNo) + ": duplicate osm_id " + f[0];
            return false;
        }
        if (!rawIndex.put(id, (int)rawId.size())) { err = "out of memory reading nodes.csv"; return false; }
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
    while (std::getline(ein, line)) {
        if (line.empty() || line == "\r") continue;
        st.rawEdges++;
        auto f = splitCsv(line);
        long long uid, vid;
        RawEdge e;
        e.csvRow = st.rawEdges - 1;
        if (f.size() != 7 || !parseLongLong(f[0], uid) || !parseLongLong(f[1], vid) ||
            !parseDouble(f[2], e.lengthM) || e.lengthM < 0 || !parseGeometry(f[6], e.geometry)) {
            st.badRows++;
            continue;
        }
        auto iu = rawIndex.find(uid), iv = rawIndex.find(vid);
        if (iu < 0 || iv < 0) { st.badRows++; continue; }
        if (uid == vid) { st.selfLoops++; continue; }
        e.u = iu;
        e.v = iv;
        e.highway = f[3];
        e.maxspeed = f[4];
        raw.push_back(std::move(e));
    }

    // ---- 3. graph cleaning: forward + reverse BFS (core/bfs.c)
    // BFS from a centre node keeps exactly the nodes that can reach the centre
    // and can be reached from it: the centre's strongly connected component.
    // The centre is the node nearest options.centre. If its component has at
    // most half of all nodes (the centre sits on an isolated stub), the next
    // nearest nodes are tried and the largest result is kept. A component with
    // more than half of all nodes must be the largest one, so the search stops.
    std::vector<int> edgeFrom(raw.size()), edgeTo(raw.size());
    for (size_t i = 0; i < raw.size(); i++) { edgeFrom[i] = raw[i].u; edgeTo[i] = raw[i].v; }

    const int n = st.rawNodes;
    const int m = (int)raw.size();
    constexpr int kMaxCentreTries = 20;
    std::vector<char> tried(n, 0), candidate(n, 0), keep(n, 0);
    int bestCount = 0;
    for (int attempt = 0; attempt < kMaxCentreTries && 2 * bestCount <= n; attempt++) {
        int centre = -1;
        double nearest = 0.0;
        for (int i = 0; i < n; i++) {                       // linear nearest-node search
            if (tried[i]) continue;
            double d = haversineMeters(rawPos[i], options.centre);
            if (centre < 0 || d < nearest) { centre = i; nearest = d; }
        }
        if (centre < 0) break;                              // every node already tried
        tried[centre] = 1;
        int count = bfs_strong_core(n, m, edgeFrom.data(), edgeTo.data(), centre, candidate.data());
        if (count < 0) { err = "graph cleaning failed (out of memory)"; return false; }
        if (count > bestCount) {
            bestCount = count;
            st.bfsCentre = centre;
            keep = candidate;
        }
    }

    // ---- 4. re-index the kept nodes 0..n-1
    OsmNetwork net;
    std::vector<int> newIndex(st.rawNodes, -1);
    for (int i = 0; i < st.rawNodes; i++) {
        if (!keep[i]) continue;
        newIndex[i] = (int)net.osmId.size();
        if (!net.indexOf.put(rawId[i], newIndex[i])) { err = "out of memory building node index"; return false; }
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
        net.csvRow.push_back(e.csvRow);
    }
    st.keptEdges = (int)net.geometry.size();

    net.stats = st;
    out = std::move(net);
    return true;
}

}  // namespace rl
