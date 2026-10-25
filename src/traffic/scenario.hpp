// scenario.hpp — "what-if" scenarios on top of the traffic state.
//
//   Normal     current conditions: live TomTom data where available, free-flow elsewhere
//   Rain       from free-flow (live data ignored): every road's speed x rainFactor
//   RushHour   from free-flow (live data ignored): main roads x rushMainFactor, others x rushOtherFactor
//   Incident   current conditions, plus chosen roads blocked or slowed to incidentSlowFactor
//
// Rain and RushHour ignore live data so a slowdown is never counted twice (if it
// is raining now, TomTom's live speeds already include it). They need no TomTom
// requests at all. Junction waits are added on scenario edges like on free-flow
// edges, because a scenario estimate contains no real waiting time.
#pragma once
#include <string>
#include <vector>

#include "traffic.hpp"

namespace rl {

enum class Scenario { Normal, RushHour, Rain, Incident };

// "normal", "rush_hour", "rain", "incident" (as sent by the website)
bool parseScenario(const std::string& text, Scenario& out);
const char* scenarioName(Scenario s);

struct ScenarioSettings {
    // FHWA Road Weather Management: heavy rain reduces free-flow speed by 6-17%
    // (US freeways). 0.88 = 12% slower, the middle of that range; Dehradun's
    // monsoon conditions may be worse. To be checked in the ETA study.
    double rainFactor = 0.88;
    // PROVISIONAL: to be replaced by the ratio measured with TomTom on Dehradun's
    // main roads during the evening rush (a daytime run measured 0.70).
    double rushMainFactor = 0.65;    // motorway, trunk, primary, secondary (and links)
    double rushOtherFactor = 0.85;   // all other roads
    double incidentSlowFactor = 0.30;  // a slowed road keeps 30% of its speed
};

struct Incident {
    std::vector<int> edges;   // graph edge indices chosen by the user
    bool blocked = false;     // true: closed; false: slowed by incidentSlowFactor
};

bool isMainRoad(const std::string& highway);

// Rain and RushHour need no TomTom data; Normal and Incident do.
bool scenarioNeedsLiveTraffic(Scenario s);

// Applies the scenario to state. Returns the number of edges changed, or -1 if
// a factor in settings is outside (0, 1] (then nothing is changed). Incident
// without an Incident list changes nothing.
int applyScenario(TrafficState& state, Scenario s, const ScenarioSettings& settings,
                  const Incident* incident = nullptr);

}  // namespace rl
