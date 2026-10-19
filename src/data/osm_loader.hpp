// osm_loader.hpp — loads the OSMnx export (data/osm/nodes.csv, edges.csv)
// into the routing Graph from src/core/graph.h.
//
// Pipeline:
//   nodes.csv -> hash map OSM id -> raw index
//   edges.csv -> raw directed edges (self-loops and bad rows skipped)
//   iterative Tarjan SCC -> keep only the largest strongly connected component
//   re-index kept nodes 0..n-1 -> graph_add_edge with free-flow travel times
//
// After loading, any node can reach any other node, so a route always exists.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/graph.h"
#include "geo.hpp"

namespace rl {

// ---- free-flow speeds -------------------------------------------------------
// PROVISIONAL defaults (km/h) for roads without a maxspeed tag (about 98% of
// edges in Dehradun's OSM data). They are deliberately lenient estimates of
// typical free-flow speeds in a dense Indian city, NOT legal limits, and must
// be calibrated against TomTom free-flow speeds or timed trips (ETA study).
double defaultSpeedKph(const std::string& highway);

// Tagged maxspeed (if it parses to a positive number) wins; else the default.
double edgeSpeedKph(const std::string& highway, const std::string& maxspeedKph);

// ---- strongly connected components --------------------------------------
// Iterative Tarjan: no recursion, so it cannot overflow the call stack on large
// graphs. Returns the component id of every node; componentCount is set.
std::vector<int> tarjanScc(int nodeCount, const std::vector<std::vector<int>>& adj,
                           int& componentCount);

// ---- loader ------------------------------------------------------------------
struct OsmLoadStats {
    int rawNodes = 0;
    int rawEdges = 0;              // edge rows read (before any filtering)
    int selfLoops = 0;             // u == v, rejected by graph_add_edge
    int badRows = 0;               // malformed or referencing unknown nodes
    int sccCount = 0;
    int keptNodes = 0;             // size of the largest SCC
    int keptEdges = 0;
    int droppedEdges = 0;          // edges touching a node outside the largest SCC
    int edgesWithMaxspeed = 0;     // of the kept edges
    int signalNodes = 0;           // signal nodes inside the largest SCC
};

class OsmNetwork {
public:
    OsmNetwork() = default;
    ~OsmNetwork();
    OsmNetwork(const OsmNetwork&) = delete;
    OsmNetwork& operator=(const OsmNetwork&) = delete;
    OsmNetwork(OsmNetwork&& other) noexcept;
    OsmNetwork& operator=(OsmNetwork&& other) noexcept;

    Graph* graph = nullptr;                          // owned; freed in destructor
    std::vector<long long> osmId;                    // node index -> OSM id
    std::vector<LatLon> pos;                         // node index -> position
    std::vector<char> isSignal;                      // node index -> 1 if signal
    std::unordered_map<long long, int> indexOf;      // OSM id -> node index
    std::vector<std::vector<LatLon>> geometry;       // edge index -> road shape
    std::vector<std::string> roadClass;              // edge index -> OSM highway
    OsmLoadStats stats;
};

// Loads <dir>/nodes.csv and <dir>/edges.csv. Returns false and sets err on failure.
bool loadOsmNetwork(const std::string& dir, OsmNetwork& out, std::string& err);

}  // namespace rl
