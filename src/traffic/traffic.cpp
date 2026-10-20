#include "traffic.hpp"

#include <algorithm>
#include <cmath>

#include "../data/json_util.hpp"

namespace rl {

// ---------------------------------------------------------------- parsing

bool parseFlowSegment(const std::string& json, FlowSegment& out, std::string& err) {
    JsonDoc doc(json);
    if (!doc) { err = "response is not valid JSON"; return false; }
    const cJSON* d = jget(doc.get(), "flowSegmentData");
    if (!cJSON_IsObject(d)) { err = "no flowSegmentData in response"; return false; }

    FlowSegment s;
    jstr(d, "frc", s.frc);
    if (!jnum(d, "currentSpeed", s.currentKph) || !jnum(d, "freeFlowSpeed", s.freeFlowKph) ||
        !jnum(d, "confidence", s.confidence)) {
        err = "missing speed or confidence fields";
        return false;
    }
    jnum(d, "currentTravelTime", s.currentTravelS);
    jnum(d, "freeFlowTravelTime", s.freeFlowTravelS);
    s.roadClosure = cJSON_IsTrue(jget(d, "roadClosure"));

    const cJSON* pts = jget(jget(d, "coordinates"), "coordinate");
    const cJSON* p = nullptr;
    cJSON_ArrayForEach(p, pts) {
        double lat, lon;
        if (jnum(p, "latitude", lat) && jnum(p, "longitude", lon)) s.shape.push_back({ lat, lon });
    }
    if (s.shape.size() < 2) { err = "segment shape has fewer than 2 points"; return false; }
    if (s.currentKph < 0 || s.freeFlowKph <= 0) { err = "invalid speeds in response"; return false; }
    out = std::move(s);
    return true;
}

// ---------------------------------------------------------------- rules

TrafficLevel levelForRatio(double ratio, const TrafficRules& r) {
    if (!std::isfinite(ratio) || ratio < 0) return TrafficLevel::Unknown;
    if (ratio >= r.lowRatio) return TrafficLevel::Low;
    if (ratio >= r.highRatio) return TrafficLevel::Medium;
    return TrafficLevel::High;
}

DataSource sourceForConfidence(double c, const TrafficRules& r) {
    if (c > r.liveConfidence) return DataSource::Live;
    if (c >= r.minConfidence) return DataSource::Historical;
    return DataSource::FreeFlow;
}

const char* levelName(TrafficLevel l) {
    switch (l) {
        case TrafficLevel::Low: return "Low";
        case TrafficLevel::Medium: return "Medium";
        case TrafficLevel::High: return "High";
        default: return "Unknown";
    }
}

const char* sourceName(DataSource s) {
    switch (s) {
        case DataSource::Live: return "live";
        case DataSource::Historical: return "typical for this time";
        default: return "estimated (free-flow)";
    }
}

// ---------------------------------------------------------------- geometry

double bearingDeg(LatLon a, LatLon b) {
    const double k = M_PI / 180.0;
    double dLon = (b.lon - a.lon) * k;
    double y = std::sin(dLon) * std::cos(b.lat * k);
    double x = std::cos(a.lat * k) * std::sin(b.lat * k) - std::sin(a.lat * k) * std::cos(b.lat * k) * std::cos(dLon);
    double deg = std::atan2(y, x) / k;
    return deg < 0 ? deg + 360.0 : deg;
}

double angleDiffDeg(double a, double b) {
    double d = std::fmod(std::fabs(a - b), 360.0);
    return d > 180.0 ? 360.0 - d : d;
}

double polylineLengthM(const std::vector<LatLon>& line) {
    double s = 0;
    for (size_t i = 1; i < line.size(); i++) s += haversineMeters(line[i - 1], line[i]);
    return s;
}

LatLon pointAlong(const std::vector<LatLon>& line, double fraction, double* bearingOut) {
    if (line.empty()) return {};
    if (line.size() == 1) { if (bearingOut) *bearingOut = 0; return line[0]; }
    double target = std::clamp(fraction, 0.0, 1.0) * polylineLengthM(line), acc = 0;
    for (size_t i = 1; i < line.size(); i++) {
        double len = haversineMeters(line[i - 1], line[i]);
        if (acc + len >= target || i + 1 == line.size()) {
            double t = len > 0 ? std::clamp((target - acc) / len, 0.0, 1.0) : 0.0;
            if (bearingOut) *bearingOut = bearingDeg(line[i - 1], line[i]);
            return { line[i - 1].lat + t * (line[i].lat - line[i - 1].lat),
                     line[i - 1].lon + t * (line[i].lon - line[i - 1].lon) };
        }
        acc += len;
    }
    return line.back();
}

double distanceToSegmentM(LatLon p, const FlowSegment& seg) {
    return pointToPolylineMeters(p, seg.shape);
}

bool edgeMatchesSegment(const std::vector<LatLon>& edge, const FlowSegment& seg,
                        const TrafficRules& r, bool* directionRejected) {
    if (directionRejected) *directionRejected = false;
    if (edge.size() < 2 || seg.shape.size() < 2) return false;

    // 1. Position: the edge's 25%, 50% and 75% points must all lie along the segment.
    double edgeBearing = 0;
    LatLon mid = pointAlong(edge, 0.5, &edgeBearing);
    if (distanceToSegmentM(mid, seg) > r.matchM) return false;
    if (distanceToSegmentM(pointAlong(edge, 0.25), seg) > r.matchM) return false;
    if (distanceToSegmentM(pointAlong(edge, 0.75), seg) > r.matchM) return false;

    // 2. Direction: compare with the segment piece nearest to the edge's midpoint.
    size_t best = 0;
    double bestD = 1e300;
    for (size_t i = 0; i + 1 < seg.shape.size(); i++) {
        if (haversineMeters(seg.shape[i], seg.shape[i + 1]) < 0.5) continue;   // skip zero-length pieces
        double d = pointToSegmentMeters(mid, seg.shape[i], seg.shape[i + 1]);
        if (d < bestD) { bestD = d; best = i; }
    }
    double segBearing = bearingDeg(seg.shape[best], seg.shape[best + 1]);
    if (angleDiffDeg(edgeBearing, segBearing) > r.maxBearingDiffDeg) {
        if (directionRejected) *directionRejected = true;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- TrafficState

TrafficState::TrafficState(OsmNetwork& net) : net_(net) { reset(); }

void TrafficState::reset() {
    edges_.assign(net_.graph ? net_.graph->edge_count : 0, EdgeTraffic{});
    for (size_t e = 0; e < edges_.size(); e++) {
        edges_[e].durationS = net_.graph->edges[e].duration_s;
        net_.graph->edges[e].traffic_index = 0.0;
    }
    directionRejected_ = 0;
}

int TrafficState::applySegment(const FlowSegment& seg, const TrafficRules& r, const std::vector<int>& candidates) {
    DataSource src = sourceForConfidence(seg.confidence, r);
    if (src == DataSource::FreeFlow) return 0;   // too uncertain: keep free-flow

    BBox box;
    for (const LatLon& p : seg.shape) box.extend(p);
    box = box.padded(r.matchM + 5.0);

    double ratio = std::max(0.0, seg.currentKph / seg.freeFlowKph);
    bool closed = seg.roadClosure || seg.currentKph <= 0.0;
    int updated = 0;
    for (int e : candidates) {
        if (e < 0 || e >= (int)edges_.size()) continue;
        const std::vector<LatLon>& shape = net_.geometry[e];
        if (shape.empty() || !box.contains(pointAlong(shape, 0.5))) continue;   // cheap pre-filter
        bool wrongWay = false;
        if (!edgeMatchesSegment(shape, seg, r, &wrongWay)) {
            if (wrongWay) directionRejected_++;
            continue;
        }
        EdgeTraffic& t = edges_[e];
        if (t.source != DataSource::FreeFlow && t.confidence >= seg.confidence) continue;   // keep better data
        Edge& ge = net_.graph->edges[e];
        t.source = src;
        t.ratio = ratio;
        t.confidence = seg.confidence;
        t.closed = closed;
        t.durationS = closed ? ge.duration_s : ge.distance_m / (seg.currentKph / 3.6);
        ge.traffic_index = closed ? 1.0 : std::clamp(1.0 - ratio, 0.0, 1.0);
        updated++;
    }
    return updated;
}

void TrafficState::computeCosts(const Weights& w, std::vector<double>& out) const {
    out.resize(edges_.size());
    for (size_t e = 0; e < edges_.size(); e++) {
        const Edge& ge = net_.graph->edges[e];
        double min = edges_[e].durationS / 60.0;
        out[e] = w.per_km * ge.distance_m / 1000.0 + w.per_minute * min +
                 w.per_congested_minute * ge.traffic_index * min + w.per_signal * ge.signal_count;
    }
}

// ---------------------------------------------------------------- sampling

bool tomtomCoversRoadClass(const std::string& h) {
    static const char* kCovered[] = { "motorway", "trunk", "primary", "secondary", "tertiary", "unclassified" };
    for (const char* c : kCovered) {
        std::string base(c);
        if (h == base || h == base + "_link") return true;
    }
    return false;   // residential, service, living_street, road, unknown
}

std::vector<LatLon> sampleRoute(const OsmNetwork& net, const std::vector<int>& routeEdges, double spacingM) {
    std::vector<LatLon> out;
    double acc = 0.0, nextAt = spacingM / 2.0;
    int firstCovered = -1;
    for (int e : routeEdges) {
        if (e < 0 || e >= (int)net.geometry.size() || !tomtomCoversRoadClass(net.roadClass[e])) continue;
        if (firstCovered < 0) firstCovered = e;
        const std::vector<LatLon>& g = net.geometry[e];
        for (size_t i = 1; i < g.size(); i++) {
            double len = haversineMeters(g[i - 1], g[i]);
            while (len > 0 && acc + len >= nextAt) {
                double t = (nextAt - acc) / len;
                out.push_back({ g[i - 1].lat + t * (g[i].lat - g[i - 1].lat), g[i - 1].lon + t * (g[i].lon - g[i - 1].lon) });
                nextAt += spacingM;
            }
            acc += len;
        }
    }
    if (out.empty() && firstCovered >= 0) out.push_back(pointAlong(net.geometry[firstCovered], 0.5));
    return out;
}

}  // namespace rl
