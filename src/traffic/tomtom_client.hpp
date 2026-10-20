// tomtom_client.hpp — TomTom Traffic Flow (Flow Segment Data) client with the
// guardrails needed for the free "Freemium" plan (2,500 non-tile requests/day):
//
//   * segment cache: a point on a segment fetched in the last cacheTtlS seconds
//     is answered from memory (one Rajpur Road answer covers ~20 km)
//   * "no coverage" cache: points TomTom has no road for are not asked again
//   * daily counter stored in a file, with our own hard stop (dailyCap)
//   * rate limit: at most one request per minIntervalS (TomTom: 5 per second)
//   * after an auth or quota error, or maxConsecutiveFailures network/server
//     failures in a row (an outage), no further requests in this run
//
// The HTTP call is injected (FetchFn), so tests run offline with canned replies.
// The API key is put into request URLs only; it never appears in messages.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "../data/http_client.hpp"
#include "traffic.hpp"

namespace rl {

enum class FetchStatus {
    Ok,                // fresh answer, TomTom road within maxSnapM
    CachedOk,          // answered from the segment cache, no request
    NoCoverage,        // TomTom has no road here (snapped too far / HTTP 400)
    CachedNoCoverage,  // known no-coverage point, no request
    DailyCapReached,   // our own daily limit reached, no request
    Disabled,          // no key, or an earlier auth/quota error in this run
    AuthError,         // HTTP 401/403: key missing, wrong or not allowed
    QuotaExceeded,     // HTTP 429: TomTom's limit reached
    Unavailable,       // network error or TomTom server error
    BadResponse        // unexpected response body
};
const char* fetchStatusName(FetchStatus s);
inline bool hasSegment(FetchStatus s) { return s == FetchStatus::Ok || s == FetchStatus::CachedOk; }

struct TomTomOptions {
    std::string apiKey;
    std::string baseUrl = "https://api.tomtom.com";
    int zoom = 10;
    int dailyCap = 2000;
    double minIntervalS = 0.25;      // 4 requests per second, under TomTom's 5
    double cacheTtlS = 300.0;        // 5 minutes
    std::string usageFile = "data/cache/tomtom_usage.txt";
    int maxConsecutiveFailures = 3;  // then assume an outage and stop asking
    TrafficRules rules;
};

struct FetchResult {
    FetchStatus status = FetchStatus::Disabled;
    int segmentIndex = -1;           // into segments() when hasSegment(status)
    double snapM = -1;               // distance from the query point to TomTom's road
    std::string message;
};

class TomTomClient {
public:
    using FetchFn = std::function<HttpResponse(const std::string& url)>;
    using ClockFn = std::function<double()>;           // seconds, monotonic
    using SleepFn = std::function<void(double)>;       // seconds
    using TodayFn = std::function<std::string()>;      // "YYYY-MM-DD"

    TomTomClient(TomTomOptions opt, FetchFn fetch, ClockFn clock = {}, SleepFn sleep = {}, TodayFn today = {});

    FetchResult fetch(LatLon p);

    const FlowSegment& segment(int i) const { return cache_[i].seg; }
    int requestsToday() const { return usedToday_; }
    int requestsThisRun() const { return requestsThisRun_; }
    int cacheHits() const { return cacheHits_; }

private:
    struct CachedSegment { FlowSegment seg; double at; };
    struct CachedMiss { LatLon p; double at; };

    void loadUsage();
    void saveUsage() const;
    std::string buildUrl(LatLon p) const;

    TomTomOptions opt_;
    FetchFn fetch_;
    ClockFn clock_;
    SleepFn sleep_;
    TodayFn today_;
    std::vector<CachedSegment> cache_;
    std::vector<CachedMiss> misses_;
    std::string usageDate_;
    int usedToday_ = 0;
    int requestsThisRun_ = 0;
    int cacheHits_ = 0;
    double lastRequestAt_ = -1e300;
    int consecutiveFailures_ = 0;
    bool disabled_ = false;
    std::string disabledReason_;
};

// Fetches traffic for sample points along a route and applies every segment to
// the route's edges. Returns one FetchResult per sample point (for logging).
std::vector<FetchResult> updateRouteTraffic(const OsmNetwork& net, TrafficState& state, TomTomClient& client,
                                            const std::vector<int>& routeEdges, const TrafficRules& rules,
                                            double spacingM = 400.0,
                                            std::vector<LatLon>* samplesOut = nullptr);

}  // namespace rl
