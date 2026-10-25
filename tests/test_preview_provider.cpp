#include "preview_provider.hpp"

#include <algorithm>
#include <iostream>
#include <string>

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

    rl::PreviewRouteProvider preview;
    rl::NullRouteProvider none;
    check(preview.isPreview(), "The preview provider declares itself as preview");
    check(!none.isPreview(), "The default provider is not a preview");
    check(!none.available(), "The default provider is not available");

    rl::RouteRequest q;
    q.fromLat = 30.2710;
    q.fromLon = 77.9955;
    q.toLat = 30.3243;
    q.toLon = 78.0412;

    auto routes = preview.compute(q);
    check(routes.size() == 3, "Three sample routes are returned");

    int recommended = 0;
    bool allSample = true, allEstimated = true, allExplained = true, shapesOk = true;
    for (const auto& r : routes) {
        recommended += r.recommended ? 1 : 0;
        allSample = allSample && r.name.find("(sample)") != std::string::npos;
        // Nothing here is measured, so no route may claim live or typical data.
        allEstimated = allEstimated && r.livePct == 0 && r.typicalPct == 0 && r.estimatedPct == 100;
        allExplained = allExplained && r.explanation.find("PREVIEW") != std::string::npos;
        shapesOk = shapesOk && r.polyline.size() >= 2 &&
                   std::abs(r.polyline.front().first - q.fromLat) < 1e-9 &&
                   std::abs(r.polyline.back().second - q.toLon) < 1e-9 && r.distanceKm > 0 &&
                   r.etaSeconds >= r.freeFlowEtaSeconds && r.junctions >= 1;
    }
    check(recommended == 1, "Exactly one route is recommended");
    check(allSample, "Every route name is labelled (sample)");
    check(allEstimated, "All data is reported as 100% estimated, never live");
    check(allExplained, "Every explanation says PREVIEW");
    check(shapesOk, "Lines run from start to end with sane numbers");

    // The priorities must change which route is recommended.
    auto recommendedIndex = [](const std::vector<rl::RouteInfo>& v) {
        for (size_t i = 0; i < v.size(); ++i) if (v[i].recommended) return static_cast<int>(i);
        return -1;
    };
    rl::RouteRequest timeOnly = q;
    timeOnly.wTime = 1; timeOnly.wDistance = 0; timeOnly.wJunctions = 0; timeOnly.wTraffic = 0;
    auto byTime = preview.compute(timeOnly);
    check(byTime[recommendedIndex(byTime)].etaSeconds ==
              std::min({byTime[0].etaSeconds, byTime[1].etaSeconds, byTime[2].etaSeconds}),
          "Time-only priorities recommend the fastest route");

    rl::RouteRequest distOnly = q;
    distOnly.wTime = 0; distOnly.wDistance = 1; distOnly.wJunctions = 0; distOnly.wTraffic = 0;
    auto byDist = preview.compute(distOnly);
    check(byDist[recommendedIndex(byDist)].distanceKm ==
              std::min({byDist[0].distanceKm, byDist[1].distanceKm, byDist[2].distanceKm}),
          "Distance-only priorities recommend the shortest route");

    // Scenarios must change the sample ETAs, so the buttons visibly do something.
    rl::RouteRequest rush = q;
    rush.scenario = "rush_hour";
    auto busy = preview.compute(rush);
    bool slower = true;
    for (size_t i = 0; i < routes.size(); ++i) slower = slower && busy[i].etaSeconds > routes[i].etaSeconds;
    check(slower, "Rush hour makes every sample route slower than normal");

    std::cout << "Preview provider tests: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
