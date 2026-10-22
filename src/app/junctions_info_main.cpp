// junctions_info_main.cpp — loads the road network and the major-junction list,
// and reports how well the junctions snap onto the roads.
//
// usage: routelens_junctions_info [RADIUS_M]     (default 50; run from the project root)
#include <cstdio>
#include <iostream>
#include <string>

#include "../data/osm_loader.hpp"
#include "../traffic/junctions.hpp"

using namespace rl;

int main(int argc, char** argv) {
    double radius = 50.0;
    if (argc > 1) {
        try { radius = std::stod(argv[1]); } catch (...) { radius = -1; }
        if (radius <= 0) { std::cerr << "usage: routelens_junctions_info [RADIUS_M]\n"; return 2; }
    }
    OsmNetwork net;
    std::string err;
    if (!loadOsmNetwork("data/osm", net, err)) { std::cerr << "error: " << err << "\n"; return 1; }
    JunctionIndex idx;
    if (!loadJunctions("data/junctions/major_junctions.csv", net, radius, idx, err)) {
        std::cerr << "error: " << err << "\n";
        return 1;
    }
    std::printf("Junctions: %zu with coordinates, %d pending (no coordinates), snap radius %.0f m\n",
                idx.junctions.size(), idx.skippedNoCoords, radius);
    int nodes = 0;
    for (const Junction& j : idx.junctions) {
        nodes += (int)j.nodes.size();
        std::printf("  %-30s %9.5f, %9.5f  %2zu node%s%s\n", j.name.c_str(), j.pos.lat, j.pos.lon, j.nodes.size(),
                    j.nodes.size() == 1 ? " " : "s", j.nodes.empty() ? "   <- NOT ON THE ROAD NETWORK" : "");
    }
    std::printf("Snapped: %zu of %zu junctions onto %d graph nodes; %d unsnapped\n",
                idx.junctions.size() - idx.unsnapped, idx.junctions.size(), nodes, idx.unsnapped);
    return 0;
}
