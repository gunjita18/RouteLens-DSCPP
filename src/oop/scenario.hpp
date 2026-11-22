// scenario.hpp — "what-if" scenarios on top of the traffic state, as a class
// hierarchy:
//
//   Scenario (abstract)
//     NormalScenario     current conditions: snapshot/live data where available, free-flow elsewhere
//     RainScenario       from free-flow (traffic data ignored): every road's speed x rainFactor
//     RushHourScenario   from free-flow (traffic data ignored): main roads x rushMainFactor, others x rushOtherFactor
//     IncidentScenario   current conditions, plus chosen roads blocked or slowed to incidentSlowFactor
//
// The server holds a std::unique_ptr<Scenario> and calls apply() without knowing
// which kind it is (runtime polymorphism); makeScenario() chooses the class.
//
// Rain and RushHour ignore traffic data so a slowdown is never counted twice (if
// it is raining now, the measured speeds already include it). They need no
// TomTom data at all. Junction waits are added on scenario edges like on
// free-flow edges, because a scenario estimate contains no real waiting time.
#pragma once
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "../traffic/traffic.hpp"

namespace rl {

// The scenario names the website sends, as a plain value (cheap to parse and compare).
enum class ScenarioKind { Normal, RushHour, Rain, Incident };

// "normal", "rush_hour", "rain", "incident" (as sent by the website)
bool parseScenario(const std::string& text, ScenarioKind& out);
const char* scenarioName(ScenarioKind s);

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
bool scenarioNeedsLiveTraffic(ScenarioKind s);

// ---- the hierarchy -----------------------------------------------------------

class Scenario {
public:
    virtual ~Scenario() = default;
    Scenario(const Scenario&) = delete;              // polymorphic base: no slicing
    Scenario& operator=(const Scenario&) = delete;

    virtual ScenarioKind kind() const = 0;

    // Changes state according to the scenario and returns the number of edges
    // changed.
    virtual int apply(TrafficState& state) const = 0;

    // True if the scenario builds on measured traffic data (Normal, Incident).
    virtual bool needsTrafficData() const = 0;

    const char* name() const { return scenarioName(kind()); }

protected:
    Scenario() = default;
};

std::ostream& operator<<(std::ostream& os, const Scenario& s);   // prints the name

// Constructors reject factors outside (0, 1] with std::invalid_argument, so an
// existing object always holds valid settings.
class NormalScenario : public Scenario {
public:
    ScenarioKind kind() const override { return ScenarioKind::Normal; }
    int apply(TrafficState& state) const override;
    bool needsTrafficData() const override { return true; }
};

class RainScenario : public Scenario {
public:
    explicit RainScenario(double factor = ScenarioSettings{}.rainFactor);
    ScenarioKind kind() const override { return ScenarioKind::Rain; }
    int apply(TrafficState& state) const override;
    bool needsTrafficData() const override { return false; }
    double factor() const { return factor_; }

private:
    double factor_;
};

class RushHourScenario : public Scenario {
public:
    RushHourScenario(double mainFactor = ScenarioSettings{}.rushMainFactor,
                     double otherFactor = ScenarioSettings{}.rushOtherFactor);
    ScenarioKind kind() const override { return ScenarioKind::RushHour; }
    int apply(TrafficState& state) const override;
    bool needsTrafficData() const override { return false; }

private:
    double mainFactor_, otherFactor_;
};

class IncidentScenario : public Scenario {
public:
    explicit IncidentScenario(Incident incident,
                              double slowFactor = ScenarioSettings{}.incidentSlowFactor);
    ScenarioKind kind() const override { return ScenarioKind::Incident; }
    int apply(TrafficState& state) const override;   // an empty edge list changes nothing
    bool needsTrafficData() const override { return true; }

private:
    Incident incident_;
    double slowFactor_;
};

// Chooses the class for kind. incident is used only for ScenarioKind::Incident
// (nullptr: an incident with no edges). Throws std::invalid_argument if a factor
// in settings is outside (0, 1].
std::unique_ptr<Scenario> makeScenario(ScenarioKind kind, const ScenarioSettings& settings,
                                       const Incident* incident = nullptr);

// Convenience wrapper: makeScenario(...)->apply(state). Returns -1 (and changes
// nothing) if a factor in settings is outside (0, 1].
int applyScenario(TrafficState& state, ScenarioKind s, const ScenarioSettings& settings,
                  const Incident* incident = nullptr);

}  // namespace rl
