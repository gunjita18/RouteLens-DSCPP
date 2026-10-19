// osm_info_main.cpp — loads the OSMnx export, prints load statistics and
// checks the C++ Tarjan result against NetworkX (meta.txt).
//
// usage: routelens_osm_info [DIR]      (default DIR = data/osm)
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "../data/osm_loader.hpp"

using namespace rl;

// Reads "key = value" from meta.txt; returns -1 if missing.
static long readMetaInt(const std::string& path, const std::string& key) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        while (!k.empty() && k.back() == ' ') k.pop_back();
        if (k == key) {
            try { return std::stol(line.substr(eq + 1)); } catch (...) { return -1; }
        }
    }
    return -1;
}

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "data/osm";

    auto t0 = std::chrono::steady_clock::now();
    OsmNetwork net;
    std::string err;
    if (!loadOsmNetwork(dir, net, err)) {
        std::cerr << "error: " << err << "\n";
        return 1;
    }
    double loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const OsmLoadStats& s = net.stats;

    std::cout << "Loaded " << dir << " in " << (long)loadMs << " ms\n"
              << "  raw:   " << s.rawNodes << " nodes, " << s.rawEdges << " edge rows\n"
              << "  skipped: " << s.selfLoops << " self-loops, " << s.badRows << " bad rows\n"
              << "  SCCs:  " << s.sccCount << " (Tarjan, iterative)\n"
              << "  kept:  " << s.keptNodes << " nodes, " << s.keptEdges << " edges (largest SCC); "
              << s.droppedEdges << " edges dropped\n"
              << "  signal nodes kept: " << s.signalNodes << "\n"
              << "  kept edges with a maxspeed tag: " << s.edgesWithMaxspeed << " ("
              << (s.keptEdges ? 100.0 * s.edgesWithMaxspeed / s.keptEdges : 0.0) << "%)\n";

    std::map<std::string, int> perClass;
    for (const auto& c : net.roadClass) perClass[c]++;
    std::cout << "  edges per road class (default speed):\n";
    for (const auto& [c, n] : perClass)
        std::printf("    %-16s %7d  (%g km/h)\n", c.c_str(), n, defaultSpeedKph(c));

    // ---- correctness check against NetworkX
    int ok = 1;
    long expectNodes = readMetaInt(dir + "/meta.txt", "largest_scc_nodes");
    long expectSccs = readMetaInt(dir + "/meta.txt", "strongly_connected_components");
    if (expectNodes < 0) {
        std::cout << "CHECK skipped: no largest_scc_nodes in meta.txt\n";
    } else {
        bool pass = expectNodes == s.keptNodes && expectSccs == s.sccCount;
        ok = pass;
        std::cout << (pass ? "CHECK PASS" : "CHECK FAIL") << ": largest SCC " << s.keptNodes
                  << " (NetworkX " << expectNodes << "), SCC count " << s.sccCount
                  << " (NetworkX " << expectSccs << ")\n";
    }

    return ok ? 0 : 1;
}
