#include "scenario.hpp"

#include <algorithm>

namespace rl {

bool parseScenario(const std::string& text, Scenario& out) {
    if (text == "normal") out = Scenario::Normal;
    else if (text == "rush_hour") out = Scenario::RushHour;
    else if (text == "rain") out = Scenario::Rain;
    else if (text == "incident") out = Scenario::Incident;
    else return false;
    return true;
}

const char* scenarioName(Scenario s) {
    switch (s) {
        case Scenario::RushHour: return "rush_hour";
        case Scenario::Rain: return "rain";
        case Scenario::Incident: return "incident";
        default: return "normal";
    }
}

bool isMainRoad(const std::string& h) {
    static const char* kMain[] = { "motorway", "trunk", "primary", "secondary" };
    for (const char* c : kMain) {
        std::string base(c);
        if (h == base || h == base + "_link") return true;
    }
    return false;
}

bool scenarioNeedsLiveTraffic(Scenario s) { return s == Scenario::Normal || s == Scenario::Incident; }

static bool validFactor(double f) { return f > 0.0 && f <= 1.0; }

int applyScenario(TrafficState& state, Scenario s, const ScenarioSettings& set, const Incident* incident) {
    if (!validFactor(set.rainFactor) || !validFactor(set.rushMainFactor) || !validFactor(set.rushOtherFactor) ||
        !validFactor(set.incidentSlowFactor))
        return -1;
    OsmNetwork& net = state.network();
    int changed = 0;

    if (s == Scenario::Rain || s == Scenario::RushHour) {
        state.reset();   // start from free-flow: live data is ignored on purpose
        for (int e = 0; e < state.edgeCount(); e++) {
            double f = s == Scenario::Rain ? set.rainFactor
                                           : (isMainRoad(net.roadClass[e]) ? set.rushMainFactor : set.rushOtherFactor);
            Edge& ge = net.graph->edges[e];
            EdgeTraffic& t = state.mutableEdge(e);
            t.source = DataSource::Scenario;
            t.ratio = f;
            t.confidence = 0.0;
            t.durationS = ge.duration_s / f;
            ge.traffic_index = 1.0 - f;
            changed++;
        }
    } else if (s == Scenario::Incident && incident) {
        for (int e : incident->edges) {   // on top of current conditions
            if (e < 0 || e >= state.edgeCount()) continue;
            Edge& ge = net.graph->edges[e];
            EdgeTraffic& t = state.mutableEdge(e);
            t.source = DataSource::Scenario;
            if (incident->blocked) {
                t.blocked = true;
                t.closed = true;
                ge.traffic_index = 1.0;
            } else {
                t.ratio *= set.incidentSlowFactor;
                t.durationS /= set.incidentSlowFactor;
                ge.traffic_index = std::clamp(1.0 - t.ratio, 0.0, 1.0);
            }
            changed++;
        }
    }
    return changed;
}

}  // namespace rl
