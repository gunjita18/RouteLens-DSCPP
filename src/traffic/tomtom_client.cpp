#include "tomtom_client.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

namespace rl {

const char* fetchStatusName(FetchStatus s) {
    switch (s) {
        case FetchStatus::Ok: return "ok";
        case FetchStatus::CachedOk: return "ok (cached)";
        case FetchStatus::NoCoverage: return "no coverage";
        case FetchStatus::CachedNoCoverage: return "no coverage (cached)";
        case FetchStatus::DailyCapReached: return "daily cap reached";
        case FetchStatus::Disabled: return "disabled";
        case FetchStatus::AuthError: return "key rejected";
        case FetchStatus::QuotaExceeded: return "TomTom quota exceeded";
        case FetchStatus::Unavailable: return "unavailable";
        default: return "bad response";
    }
}

static double steadyNowS() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static std::string localDate() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

TomTomClient::TomTomClient(TomTomOptions opt, FetchFn fetch, ClockFn clock, SleepFn sleep, TodayFn today)
    : opt_(std::move(opt)), fetch_(std::move(fetch)), clock_(std::move(clock)), sleep_(std::move(sleep)),
      today_(std::move(today)) {
    if (!clock_) clock_ = steadyNowS;
    if (!sleep_) sleep_ = [](double s) { std::this_thread::sleep_for(std::chrono::duration<double>(s)); };
    if (!today_) today_ = localDate;
    if (opt_.apiKey.empty()) { disabled_ = true; disabledReason_ = "no TomTom API key configured"; }
    loadUsage();
}

void TomTomClient::loadUsage() {
    usageDate_ = today_();
    usedToday_ = 0;
    std::ifstream in(opt_.usageFile);
    std::string date;
    int count = 0;
    if (in >> date >> count && date == usageDate_ && count >= 0) usedToday_ = count;
}

void TomTomClient::saveUsage() const {
    std::error_code ec;
    std::filesystem::path p(opt_.usageFile);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(opt_.usageFile, std::ios::trunc);
    out << usageDate_ << " " << usedToday_ << "\n";
}

std::string TomTomClient::buildUrl(LatLon p) const {
    char coords[64];
    std::snprintf(coords, sizeof coords, "%.6f,%.6f", p.lat, p.lon);
    return opt_.baseUrl + "/traffic/services/4/flowSegmentData/absolute/" + std::to_string(opt_.zoom) +
           "/json?point=" + coords + "&unit=KMPH&key=" + urlEncode(opt_.apiKey);
}

FetchResult TomTomClient::fetch(LatLon p) {
    FetchResult r;
    if (disabled_) { r.status = FetchStatus::Disabled; r.message = disabledReason_; return r; }
    const TrafficRules& rules = opt_.rules;
    double now = clock_();

    // 1. Segment cache: is p on a road we already have fresh data for?
    for (size_t i = 0; i < cache_.size(); i++) {
        if (now - cache_[i].at > opt_.cacheTtlS) continue;
        double d = distanceToSegmentM(p, cache_[i].seg);
        if (d <= rules.maxSnapM && (r.segmentIndex < 0 || d < r.snapM)) { r.segmentIndex = (int)i; r.snapM = d; }
    }
    if (r.segmentIndex >= 0) { r.status = FetchStatus::CachedOk; cacheHits_++; return r; }

    // 2. Known no-coverage point?
    for (const CachedMiss& m : misses_) {
        if (now - m.at <= opt_.cacheTtlS && haversineMeters(p, m.p) <= rules.maxSnapM) {
            r.status = FetchStatus::CachedNoCoverage;
            cacheHits_++;
            return r;
        }
    }

    // 3. Our own daily limit (resets when the date changes).
    std::string today = today_();
    if (today != usageDate_) { usageDate_ = today; usedToday_ = 0; }
    if (usedToday_ >= opt_.dailyCap) {
        r.status = FetchStatus::DailyCapReached;
        r.message = "daily cap of " + std::to_string(opt_.dailyCap) + " TomTom requests reached; using free-flow";
        return r;
    }

    // 4. Rate limit, then one request (counted even if it fails, to stay safe).
    double wait = opt_.minIntervalS - (now - lastRequestAt_);
    if (wait > 0) sleep_(wait);
    lastRequestAt_ = clock_();
    HttpResponse resp = fetch_(buildUrl(p));
    usedToday_++;
    requestsThisRun_++;
    saveUsage();

    // 5. Interpret the answer.
    bool failed = !resp.error.empty() || resp.status >= 500;
    consecutiveFailures_ = failed ? consecutiveFailures_ + 1 : 0;
    if (failed && consecutiveFailures_ >= opt_.maxConsecutiveFailures) {
        disabled_ = true;
        disabledReason_ = "TomTom unreachable " + std::to_string(consecutiveFailures_) +
                          " times in a row; using free-flow for this run";
    }
    if (!resp.error.empty()) {
        r.status = FetchStatus::Unavailable;
        r.message = "network error: " + resp.error;
        return r;
    }
    if (resp.status == 401 || resp.status == 403) {
        disabled_ = true;
        disabledReason_ = "TomTom rejected the API key (HTTP " + std::to_string(resp.status) + ")";
        r.status = FetchStatus::AuthError;
        r.message = disabledReason_;
        return r;
    }
    if (resp.status == 429) {
        disabled_ = true;
        disabledReason_ = "TomTom request limit reached (HTTP 429)";
        r.status = FetchStatus::QuotaExceeded;
        r.message = disabledReason_;
        return r;
    }
    if (resp.status == 400) {   // e.g. no road near the point
        misses_.push_back({ p, now });
        r.status = FetchStatus::NoCoverage;
        r.message = "TomTom has no road near this point (HTTP 400)";
        return r;
    }
    if (resp.status >= 500) { r.status = FetchStatus::Unavailable; r.message = "TomTom server error (HTTP " + std::to_string(resp.status) + ")"; return r; }
    if (resp.status < 200 || resp.status >= 300) { r.status = FetchStatus::BadResponse; r.message = "unexpected HTTP " + std::to_string(resp.status); return r; }

    FlowSegment seg;
    std::string err;
    if (!parseFlowSegment(resp.body, seg, err)) { r.status = FetchStatus::BadResponse; r.message = err; return r; }

    // Cache the segment even if it is far from p: it is valid data for that road.
    int idx = -1;
    for (size_t i = 0; i < cache_.size(); i++) {
        const auto& s = cache_[i].seg.shape;
        if (s.size() == seg.shape.size() && haversineMeters(s.front(), seg.shape.front()) < 1.0 &&
            haversineMeters(s.back(), seg.shape.back()) < 1.0) { idx = (int)i; break; }
    }
    if (idx < 0) { cache_.push_back({ std::move(seg), now }); idx = (int)cache_.size() - 1; }
    else cache_[idx] = { std::move(seg), now };

    r.snapM = distanceToSegmentM(p, cache_[idx].seg);
    if (r.snapM > rules.maxSnapM) {
        misses_.push_back({ p, now });
        r.status = FetchStatus::NoCoverage;
        r.message = "nearest TomTom road is " + std::to_string((int)r.snapM) + " m away";
        return r;
    }
    r.status = FetchStatus::Ok;
    r.segmentIndex = idx;
    return r;
}

std::vector<FetchResult> updateRouteTraffic(const OsmNetwork& net, TrafficState& state, TomTomClient& client,
                                            const std::vector<int>& routeEdges, const TrafficRules& rules,
                                            double spacingM, std::vector<LatLon>* samplesOut) {
    std::vector<LatLon> samples = sampleRoute(net, routeEdges, spacingM);
    if (samplesOut) *samplesOut = samples;
    std::vector<FetchResult> results;
    std::set<int> applied;
    for (const LatLon& p : samples) {
        FetchResult r = client.fetch(p);
        if (hasSegment(r.status) && applied.insert(r.segmentIndex).second)
            state.applySegment(client.segment(r.segmentIndex), rules, routeEdges);
        results.push_back(r);
    }
    return results;
}

}  // namespace rl
