#ifndef ROUTELENS_PREVIEW_PROVIDER_HPP
#define ROUTELENS_PREVIEW_PROVIDER_HPP

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "route_provider.hpp"

namespace rl {

// DEVELOPMENT AND INTERFACE PREVIEW ONLY. Not a routing engine.
//
// Returns three SAMPLE routes drawn as curved lines between the two clicked
// points, with simple made-up numbers, so the whole interface (cards, chart,
// popups, sliders, scenario buttons) can be shown before the real engine exists.
// Nothing here is computed from the road network or from live traffic.
// It is off unless the server is started with ROUTELENS_PREVIEW=1, every
// response says "preview": true, the website shows a permanent warning banner,
// route names end in "(sample)", and all data is reported as 100% estimated.
class PreviewRouteProvider : public RouteProvider {
public:
    bool available() const override { return true; }
    bool isPreview() const override { return true; }

    std::vector<RouteInfo> compute(const RouteRequest& q) override {
        struct Shape {
            const char* name;
            double bulge;          // sideways bulge of the drawn line, in degrees
            double speedKph;       // sample free-flow speed
            double junctionsPerKm;
            const char* level;
        };
        static const Shape shapes[3] = {
            {"Direct (sample)", 0.0, 28.0, 1.6, "Medium"},
            {"Via north (sample)", 0.006, 36.0, 0.9, "Low"},
            {"Via south (sample)", -0.006, 31.0, 1.2, "High"},
        };

        std::vector<RouteInfo> routes;
        std::vector<double> levelFactor;
        for (int k = 0; k < 3; ++k) {
            const Shape& s = shapes[k];
            RouteInfo r;
            r.name = s.name;
            r.polyline = curve(q, s.bulge);
            r.distanceKm = lengthKm(r.polyline);
            r.freeFlowEtaSeconds = r.distanceKm / s.speedKph * 3600.0;
            const double lf = factorFor(s.level);
            levelFactor.push_back(lf);
            r.etaSeconds = r.freeFlowEtaSeconds * lf * scenarioFactor(q.scenario, k);
            r.junctions = std::max(1, static_cast<int>(std::lround(r.distanceKm * s.junctionsPerKm)));
            r.trafficLevel = s.level;
            r.estimatedPct = 100.0;  // nothing here is measured
            routes.push_back(std::move(r));
        }

        // Weighted score: each metric divided by the best (lowest) value, so the
        // sliders visibly change which route is recommended.
        auto minOf = [&](auto get) {
            double m = 1e300;
            for (size_t i = 0; i < routes.size(); ++i) m = std::min(m, get(i));
            return m > 0 ? m : 1.0;
        };
        const double mEta = minOf([&](size_t i) { return routes[i].etaSeconds; });
        const double mKm = minOf([&](size_t i) { return routes[i].distanceKm; });
        const double mJ = minOf([&](size_t i) { return static_cast<double>(routes[i].junctions); });
        const double mT = minOf([&](size_t i) { return levelFactor[i]; });
        size_t best = 0;
        for (size_t i = 0; i < routes.size(); ++i) {
            routes[i].score = q.wTime * routes[i].etaSeconds / mEta + q.wDistance * routes[i].distanceKm / mKm +
                              q.wJunctions * routes[i].junctions / mJ + q.wTraffic * levelFactor[i] / mT;
            if (routes[i].score < routes[best].score) best = i;
        }
        routes[best].recommended = true;

        char buf[300];
        for (size_t i = 0; i < routes.size(); ++i) {
            if (i == best) {
                std::snprintf(buf, sizeof buf,
                              "PREVIEW sample, not computed from the road network: lowest weighted score "
                              "(%.2f) for your priorities (time %.0f%%, distance %.0f%%, junctions %.0f%%, "
                              "congestion %.0f%%).",
                              routes[i].score, q.wTime * 100, q.wDistance * 100, q.wJunctions * 100,
                              q.wTraffic * 100);
            } else {
                std::snprintf(buf, sizeof buf,
                              "PREVIEW sample: weighted score %.2f, higher than the best (%.2f) for your priorities.",
                              routes[i].score, routes[best].score);
            }
            routes[i].explanation = buf;
        }
        return routes;
    }

private:
    static double factorFor(const std::string& level) {
        return level == "Low" ? 1.0 : (level == "Medium" ? 1.15 : 1.35);
    }

    // The incident scenario blocks the direct route (index 0) harder than the others.
    static double scenarioFactor(const std::string& scenario, int route) {
        if (scenario == "rush_hour") return 1.3;
        if (scenario == "rain") return 1.15;
        if (scenario == "incident") return route == 0 ? 1.6 : 1.1;
        return 1.0;
    }

    static std::vector<std::pair<double, double>> curve(const RouteRequest& q, double bulgeDeg) {
        const int n = 16;
        const double dLat = q.toLat - q.fromLat;
        const double dLon = q.toLon - q.fromLon;
        const double len = std::hypot(dLat, dLon);
        const double pLat = len > 0 ? -dLon / len : 0.0;  // perpendicular, unit length
        const double pLon = len > 0 ? dLat / len : 0.0;
        std::vector<std::pair<double, double>> pts;
        for (int i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / (n - 1);
            const double side = bulgeDeg * std::sin(3.14159265358979323846 * t);
            pts.emplace_back(q.fromLat + t * dLat + side * pLat, q.fromLon + t * dLon + side * pLon);
        }
        return pts;
    }

    static double haversineKm(std::pair<double, double> a, std::pair<double, double> b) {
        const double k = 3.14159265358979323846 / 180.0;
        const double dLat = (b.first - a.first) * k;
        const double dLon = (b.second - a.second) * k;
        const double h = std::sin(dLat / 2) * std::sin(dLat / 2) +
                         std::cos(a.first * k) * std::cos(b.first * k) * std::sin(dLon / 2) * std::sin(dLon / 2);
        return 6371.0088 * 2 * std::asin(std::min(1.0, std::sqrt(h)));
    }

    static double lengthKm(const std::vector<std::pair<double, double>>& pts) {
        double km = 0;
        for (size_t i = 0; i + 1 < pts.size(); ++i) km += haversineKm(pts[i], pts[i + 1]);
        return km;
    }
};

}  // namespace rl

#endif
