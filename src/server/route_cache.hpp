#ifndef ROUTELENS_ROUTE_CACHE_HPP
#define ROUTELENS_ROUTE_CACHE_HPP

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../traffic/lru_cache.hpp"
#include "request_params.hpp"
#include "route_provider.hpp"

namespace rl {

// Caches route results for a few minutes, so the same query (same two junctions,
// scenario and priorities) does not recompute routes or spend TomTom requests
// again. Built on LRUCache (hash map + doubly linked list), which has no expiry,
// so every entry carries its store time and is treated as a miss once older than
// the time-to-live. The 5 minutes match the traffic client's own cache TTL.
// LRUCache is not thread-safe and Crow is multithreaded, so all access is locked.
class RouteCache {
public:
    using ClockFn = std::function<double()>;  // seconds, monotonic

    RouteCache(std::size_t capacity, double ttlSeconds, ClockFn clock = {})
        : cache_(capacity), ttl_(ttlSeconds), clock_(clock ? std::move(clock) : steadyNowS) {}

    // Same snapped junctions, scenario and priorities give the same key.
    // Priorities are rounded to 3 decimals so tiny slider jitter does not matter.
    static std::string makeKey(const RouteRequest& r) {
        char buf[192];
        std::snprintf(buf, sizeof buf, "%d|%d|%s|%.3f|%.3f|%.3f|%.3f", r.fromNode, r.toNode,
                      r.scenario.c_str(), r.wTime, r.wDistance, r.wJunctions, r.wTraffic);
        return buf;
    }

    std::optional<std::vector<RouteInfo>> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto entry = cache_.get(key);
        if (entry) {
            if (clock_() - entry->storedAt <= ttl_) {
                ++hits_;
                return entry->routes;
            }
            cache_.erase(key);  // expired
        }
        ++misses_;
        return std::nullopt;
    }

    void put(const std::string& key, std::vector<RouteInfo> routes) {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.put(key, Entry{clock_(), std::move(routes)});
    }

    std::size_t size() const { std::lock_guard<std::mutex> l(mutex_); return cache_.size(); }
    std::size_t capacity() const { std::lock_guard<std::mutex> l(mutex_); return cache_.capacity(); }
    std::size_t hits() const { std::lock_guard<std::mutex> l(mutex_); return hits_; }
    std::size_t misses() const { std::lock_guard<std::mutex> l(mutex_); return misses_; }
    double ttlSeconds() const { return ttl_; }

private:
    struct Entry {
        double storedAt = 0;
        std::vector<RouteInfo> routes;
    };

    static double steadyNowS() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    mutable std::mutex mutex_;
    LRUCache<std::string, Entry> cache_;
    double ttl_;
    ClockFn clock_;
    std::size_t hits_ = 0;
    std::size_t misses_ = 0;
};

}  // namespace rl

#endif
