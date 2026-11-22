// osm_loader.hpp — loads the OSMnx export (data/osm/nodes.csv, edges.csv)
// into the routing Graph from src/core/graph.h.
//
// Pipeline:
//   nodes.csv -> hash table OSM id -> raw index (hand-written C, core/hashtable.c)
//   edges.csv -> raw directed edges (self-loops and bad rows skipped)
//   forward + reverse BFS (core/bfs.c) -> keep the nodes that can reach the
//       centre and be reached from it, i.e. its strongly connected component
//   re-index kept nodes 0..n-1 -> graph_add_edge with free-flow travel times
//
// After loading, any node can reach any other node, so a route always exists.
#pragma once
#include <string>
#include <vector>

#include "../core/graph.h"
#include "../core/hashtable.h"
#include "geo.hpp"

namespace rl {

// Owns a C HashTable mapping OSM node id -> node index (RAII: freed in the
// destructor, move-only). find() returns -1 for an unknown id.
class IdIndex {
public:
    IdIndex();                              // throws std::bad_alloc if memory runs out
    ~IdIndex();
    IdIndex(const IdIndex&) = delete;
    IdIndex& operator=(const IdIndex&) = delete;
    IdIndex(IdIndex&& other) noexcept;
    IdIndex& operator=(IdIndex&& other) noexcept;

    bool put(long long osmId, int index);   // false only on out-of-memory
    int find(long long osmId) const;        // -1 if absent
    int size() const { return table_.count; }

private:
    HashTable table_;
};

// Where graph cleaning starts. BFS from a node keeps that node's strongly
// connected component, so the centre should lie in the main road network.
struct OsmLoadOptions {
    LatLon centre = { 30.324310, 78.041830 };   // Clock Tower, Dehradun (data/junctions)
};

// ---- free-flow speeds -------------------------------------------------------
// PROVISIONAL defaults (km/h) for roads without a maxspeed tag (about 98% of
// edges in Dehradun's OSM data). They are deliberately lenient estimates of
// typical free-flow speeds in a dense Indian city, NOT legal limits, and must
// be calibrated against TomTom free-flow speeds or timed trips (ETA study).
double defaultSpeedKph(const std::string& highway);

// Tagged maxspeed (if it parses to a positive number) wins; else the default.
double edgeSpeedKph(const std::string& highway, const std::string& maxspeedKph);

// ---- loader ------------------------------------------------------------------
struct OsmLoadStats {
    int rawNodes = 0;
    int rawEdges = 0;              // edge rows read (before any filtering)
    int selfLoops = 0;             // u == v, rejected by graph_add_edge
    int badRows = 0;               // malformed or referencing unknown nodes
    int bfsCentre = -1;            // raw index of the node cleaning started from
    int keptNodes = 0;             // size of the kept strongly connected component
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
    IdIndex indexOf;                                 // OSM id -> node index
    std::vector<std::vector<LatLon>> geometry;       // edge index -> road shape
    std::vector<std::string> roadClass;              // edge index -> OSM highway
    std::vector<int> csvRow;                         // edge index -> data row in edges.csv
                                                     // (the edge_id used by traffic.csv)
    OsmLoadStats stats;
};

// Loads <dir>/nodes.csv and <dir>/edges.csv. Returns false and sets err on failure.
bool loadOsmNetwork(const std::string& dir, OsmNetwork& out, std::string& err,
                    const OsmLoadOptions& options = OsmLoadOptions());

}  // namespace rl
