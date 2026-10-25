// junctions.hpp — major junctions (places where traffic is usually held up,
// by signals or traffic police) loaded from data/junctions/major_junctions.csv.
//
// OpenStreetMap has only 6 signals mapped in Dehradun, so the ETA counts these
// documented junctions instead (41 from the UKMRC CMP 2024 annexure).
//
// Snapping: a junction claims every graph node within radiusM of its coordinate
// (divided roads give one junction several nodes); a node near two junctions
// goes to the closer one. Junctions with no node in range are "unsnapped".
// Counting: a junction counts when a route ARRIVES at one of its nodes, and
// only once per route even if the route crosses several of its nodes.
#pragma once
#include <string>
#include <vector>

#include "../data/geo.hpp"
#include "../data/osm_loader.hpp"

namespace rl {

struct Junction {
    std::string name;
    LatLon pos;
    std::string status;       // e.g. documented_2024
    std::vector<int> nodes;   // graph nodes within the snap radius
};

struct JunctionIndex {
    std::vector<Junction> junctions;
    std::vector<int> junctionAtNode;   // node index -> junction index, or -1
    int skippedNoCoords = 0;           // rows without coordinates (pending)
    int unsnapped = 0;                 // junctions with no graph node in range

    int junctionOf(int node) const {
        return node >= 0 && node < (int)junctionAtNode.size() ? junctionAtNode[node] : -1;
    }
};

// Loads the CSV (columns include name, lat, lon, status) and snaps to net.
bool loadJunctions(const std::string& csvPath, const OsmNetwork& net, double radiusM, JunctionIndex& out,
                   std::string& err);

// Junctions the route arrives at, in order, each listed once.
std::vector<int> junctionsOnRoute(const OsmNetwork& net, const JunctionIndex& idx, const std::vector<int>& routeEdges);

}  // namespace rl
