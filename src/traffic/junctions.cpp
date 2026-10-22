#include "junctions.hpp"

#include <cmath>
#include <fstream>

namespace rl {

// One CSV line; supports quoted fields containing commas and "" escapes.
static std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> f;
    std::string cur;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; i++; }
            else if (c == '"') quoted = false;
            else cur += c;
        } else if (c == '"') quoted = true;
        else if (c == ',') { f.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    f.push_back(cur);
    return f;
}

static bool toDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    try {
        size_t pos = 0;
        out = std::stod(s, &pos);
        return pos == s.size() && std::isfinite(out);
    } catch (...) {
        return false;
    }
}

bool loadJunctions(const std::string& csvPath, const OsmNetwork& net, double radiusM, JunctionIndex& out,
                   std::string& err) {
    std::ifstream in(csvPath);
    if (!in) { err = "cannot open " + csvPath; return false; }
    std::string line;
    if (!std::getline(in, line)) { err = csvPath + " is empty"; return false; }
    std::vector<std::string> hdr = splitCsvLine(line);
    int cName = -1, cLat = -1, cLon = -1, cStatus = -1;
    for (int i = 0; i < (int)hdr.size(); i++) {
        if (hdr[i] == "name") cName = i;
        else if (hdr[i] == "lat") cLat = i;
        else if (hdr[i] == "lon") cLon = i;
        else if (hdr[i] == "status") cStatus = i;
    }
    if (cName < 0 || cLat < 0 || cLon < 0) { err = csvPath + ": header needs name, lat, lon"; return false; }

    JunctionIndex idx;
    int lineNo = 1;
    while (std::getline(in, line)) {
        lineNo++;
        if (line.empty() || line == "\r") continue;
        std::vector<std::string> f = splitCsvLine(line);
        int need = std::max(cName, std::max(cLat, cLon));
        if ((int)f.size() <= need) { err = csvPath + ":" + std::to_string(lineNo) + ": too few columns"; return false; }
        double lat, lon;
        if (f[cLat].empty() || f[cLon].empty()) { idx.skippedNoCoords++; continue; }   // pending
        if (!toDouble(f[cLat], lat) || !toDouble(f[cLon], lon) || std::fabs(lat) > 90 || std::fabs(lon) > 180) {
            err = csvPath + ":" + std::to_string(lineNo) + ": bad coordinates";
            return false;
        }
        Junction j;
        j.name = f[cName];
        j.pos = { lat, lon };
        if (cStatus >= 0 && cStatus < (int)f.size()) j.status = f[cStatus];
        idx.junctions.push_back(std::move(j));
    }

    // Snap: every node within radiusM, each node to its nearest junction.
    const int n = (int)net.pos.size();
    idx.junctionAtNode.assign(n, -1);
    std::vector<double> best(n, 1e300);
    for (int k = 0; k < (int)idx.junctions.size(); k++) {
        const LatLon c = idx.junctions[k].pos;
        const double dLat = radiusM / 111000.0 * 1.5;
        const double dLon = dLat / std::max(0.1, std::cos(c.lat * M_PI / 180.0));
        for (int i = 0; i < n; i++) {
            const LatLon p = net.pos[i];
            if (std::fabs(p.lat - c.lat) > dLat || std::fabs(p.lon - c.lon) > dLon) continue;   // cheap pre-filter
            double d = haversineMeters(c, p);
            if (d <= radiusM && d < best[i]) { best[i] = d; idx.junctionAtNode[i] = k; }
        }
    }
    for (int i = 0; i < n; i++)
        if (idx.junctionAtNode[i] >= 0) idx.junctions[idx.junctionAtNode[i]].nodes.push_back(i);
    for (const Junction& j : idx.junctions)
        if (j.nodes.empty()) idx.unsnapped++;

    out = std::move(idx);
    return true;
}

std::vector<int> junctionsOnRoute(const OsmNetwork& net, const JunctionIndex& idx, const std::vector<int>& routeEdges) {
    std::vector<int> out;
    std::vector<char> seen(idx.junctions.size(), 0);
    for (int e : routeEdges) {
        if (e < 0 || e >= net.graph->edge_count) continue;
        int j = idx.junctionOf(net.graph->edges[e].to);
        if (j >= 0 && !seen[j]) { seen[j] = 1; out.push_back(j); }
    }
    return out;
}

}  // namespace rl
