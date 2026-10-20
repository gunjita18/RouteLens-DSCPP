#include "request_params.hpp"

#include <iostream>
#include <map>
#include <string>

namespace {

rl::ParamGetter from(const std::map<std::string, std::string>& m) {
    return [m](const std::string& k) -> std::optional<std::string> {
        auto it = m.find(k);
        if (it == m.end()) {
            return std::nullopt;
        }
        return it->second;
    };
}

}  // namespace

int main() {
    int checks = 0;
    int failures = 0;

    auto check = [&](bool condition, const char* message) {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    };

    const std::map<std::string, std::string> valid = {
        {"from_lat", "30.32"}, {"from_lon", "78.03"},
        {"to_lat", "30.40"},   {"to_lon", "78.10"}};
    std::string err;

    auto ok = rl::parseRouteRequest(from(valid), err);
    check(ok.has_value(), "Valid request should parse");
    check(ok && ok->scenario == "normal", "Scenario defaults to normal");
    check(ok && ok->wTime > 0.249 && ok->wTime < 0.251, "Default weights are equal");

    auto m = valid;
    m.erase("to_lon");
    check(!rl::parseRouteRequest(from(m), err), "Missing coordinate is rejected");

    m = valid;
    m["from_lat"] = "abc";
    check(!rl::parseRouteRequest(from(m), err), "Non-numeric coordinate is rejected");

    m = valid;
    m["from_lat"] = "nan";
    check(!rl::parseRouteRequest(from(m), err), "NaN coordinate is rejected");

    m = valid;
    m["to_lat"] = "28.61";
    check(!rl::parseRouteRequest(from(m), err), "Point outside Dehradun is rejected");

    m = valid;
    m["w_time"] = "3";
    m["w_distance"] = "1";
    m["w_signals"] = "0";
    m["w_traffic"] = "0";
    auto w = rl::parseRouteRequest(from(m), err);
    check(w && w->wTime > 0.749 && w->wTime < 0.751, "Weights are normalised to sum 1");
    check(w && w->wSignals == 0.0, "Zero weight is allowed");

    m = valid;
    m["w_time"] = m["w_distance"] = m["w_signals"] = m["w_traffic"] = "0";
    check(!rl::parseRouteRequest(from(m), err), "All-zero weights are rejected");

    m = valid;
    m["w_time"] = "-1";
    check(!rl::parseRouteRequest(from(m), err), "Negative weight is rejected");

    m = valid;
    m["scenario"] = "rain";
    auto sc = rl::parseRouteRequest(from(m), err);
    check(sc && sc->scenario == "rain", "Known scenario is accepted");

    m = valid;
    m["scenario"] = "tsunami";
    check(!rl::parseRouteRequest(from(m), err), "Unknown scenario is rejected");

    std::cout << "Request param tests: " << checks << " checks, " << failures
              << " failures\n";
    return failures == 0 ? 0 : 1;
}
