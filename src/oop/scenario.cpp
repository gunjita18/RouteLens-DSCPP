#include "scenario.hpp"

#include <algorithm>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace rl {

// ---------------------------------------------------------------- names

bool parseScenario(const std::string& text, ScenarioKind& out) {
    if (text == "normal") out = ScenarioKind::Normal;
    else if (text == "rush_hour") out = ScenarioKind::RushHour;
    else if (text == "rain") out = ScenarioKind::Rain;
    else if (text == "incident") out = ScenarioKind::Incident;
    else return false;
    return true;
}

const char* scenarioName(ScenarioKind s) {
    switch (s) {
        case ScenarioKind::RushHour: return "rush_hour";
        case ScenarioKind::Rain: return "rain";
        case ScenarioKind::Incident: return "incident";
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

bool scenarioNeedsLiveTraffic(ScenarioKind s) {
    return s == ScenarioKind::Normal || s == ScenarioKind::Incident;
}

std::ostream& operator<<(std::ostream& os, const Scenario& s) { return os << s.name(); }

// ---------------------------------------------------------------- helpers

namespace {

bool validFactor(double f) { return f > 0.0 && f <= 1.0; }

double checkedFactor(double f, const char* what) {
    if (!validFactor(f)) throw std::invalid_argument(std::string(what) + " must be in (0, 1]");
    return f;
}

// Replaces every edge's traffic by a scenario estimate: speed = free-flow x factor(e).
template <typename FactorOf>
int applyEverywhere(TrafficState& state, FactorOf factorOf) {
    OsmNetwork& net = state.network();
    state.reset();   // start from free-flow: measured data is ignored on purpose
    int changed = 0;
    for (int e = 0; e < state.edgeCount(); e++) {
        double f = factorOf(e);
        Edge& ge = net.graph->edges[e];
        EdgeTraffic& t = state.mutableEdge(e);
        t.source = DataSource::Scenario;
        t.ratio = f;
        t.confidence = 0.0;
        t.durationS = ge.duration_s / f;
        ge.traffic_index = 1.0 - f;
        changed++;
    }
    return changed;
}

}  // namespace

// ---------------------------------------------------------------- derived classes

int NormalScenario::apply(TrafficState&) const { return 0; }   // current conditions: nothing to change

RainScenario::RainScenario(double factor) : factor_(checkedFactor(factor, "rain factor")) {}

int RainScenario::apply(TrafficState& state) const {
    return applyEverywhere(state, [this](int) { return factor_; });
}

RushHourScenario::RushHourScenario(double mainFactor, double otherFactor)
    : mainFactor_(checkedFactor(mainFactor, "rush-hour main-road factor")),
      otherFactor_(checkedFactor(otherFactor, "rush-hour other-road factor")) {}

int RushHourScenario::apply(TrafficState& state) const {
    const OsmNetwork& net = state.network();
    return applyEverywhere(state, [&](int e) {
        return isMainRoad(net.roadClass[e]) ? mainFactor_ : otherFactor_;
    });
}

IncidentScenario::IncidentScenario(Incident incident, double slowFactor)
    : incident_(std::move(incident)), slowFactor_(checkedFactor(slowFactor, "incident slow factor")) {}

int IncidentScenario::apply(TrafficState& state) const {
    OsmNetwork& net = state.network();
    int changed = 0;
    for (int e : incident_.edges) {   // on top of current conditions
        if (e < 0 || e >= state.edgeCount()) continue;
        Edge& ge = net.graph->edges[e];
        EdgeTraffic& t = state.mutableEdge(e);
        t.source = DataSource::Scenario;
        if (incident_.blocked) {
            t.blocked = true;
            t.closed = true;
            ge.traffic_index = 1.0;
        } else {
            t.ratio *= slowFactor_;
            t.durationS /= slowFactor_;
            ge.traffic_index = std::clamp(1.0 - t.ratio, 0.0, 1.0);
        }
        changed++;
    }
    return changed;
}

// ---------------------------------------------------------------- factory

std::unique_ptr<Scenario> makeScenario(ScenarioKind kind, const ScenarioSettings& set,
                                       const Incident* incident) {
    // All four factors are checked, whichever class is chosen, so a bad settings
    // object is rejected the same way every time.
    checkedFactor(set.rainFactor, "rain factor");
    checkedFactor(set.rushMainFactor, "rush-hour main-road factor");
    checkedFactor(set.rushOtherFactor, "rush-hour other-road factor");
    checkedFactor(set.incidentSlowFactor, "incident slow factor");
    switch (kind) {
        case ScenarioKind::Rain:
            return std::make_unique<RainScenario>(set.rainFactor);
        case ScenarioKind::RushHour:
            return std::make_unique<RushHourScenario>(set.rushMainFactor, set.rushOtherFactor);
        case ScenarioKind::Incident:
            return std::make_unique<IncidentScenario>(incident ? *incident : Incident{},
                                                      set.incidentSlowFactor);
        default:
            return std::make_unique<NormalScenario>();
    }
}

int applyScenario(TrafficState& state, ScenarioKind s, const ScenarioSettings& set,
                  const Incident* incident) {
    try {
        return makeScenario(s, set, incident)->apply(state);
    } catch (const std::invalid_argument&) {
        return -1;   // invalid settings: nothing was changed
    }
}

}  // namespace rl
