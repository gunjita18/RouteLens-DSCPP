#include "route_cache.hpp"

#include <iostream>
#include <thread>
#include <vector>

namespace {

std::vector<rl::RouteInfo> sample(const char* name) {
    rl::RouteInfo r;
    r.name = name;
    return {r};
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

    double now = 1000.0;  // fake clock, seconds
    auto clock = [&] { return now; };

    // ---- hit, miss, statistics ------------------------------------------------
    {
        rl::RouteCache cache(4, 300.0, clock);
        check(!cache.get("a").has_value(), "Unknown key is a miss");
        cache.put("a", sample("A"));
        auto hit = cache.get("a");
        check(hit.has_value() && hit->size() == 1 && (*hit)[0].name == "A", "Stored routes are returned");
        check(cache.hits() == 1 && cache.misses() == 1, "Hits and misses are counted");
        check(cache.size() == 1 && cache.capacity() == 4, "Size and capacity are reported");
    }

    // ---- expiry (the LRU itself has none) ------------------------------------
    {
        rl::RouteCache cache(4, 300.0, clock);
        now = 1000.0;
        cache.put("a", sample("A"));
        now = 1299.0;
        check(cache.get("a").has_value(), "Entry just inside the 5-minute TTL is still a hit");
        now = 1301.0;
        check(!cache.get("a").has_value(), "Entry older than the TTL is a miss");
        check(cache.size() == 0, "Expired entry is removed from the cache");
        cache.put("a", sample("A2"));
        check(cache.get("a").has_value(), "A fresh entry can be stored again after expiry");
    }

    // ---- least-recently-used eviction -----------------------------------------
    {
        rl::RouteCache cache(2, 300.0, clock);
        now = 2000.0;
        cache.put("a", sample("A"));
        cache.put("b", sample("B"));
        cache.get("a");              // "a" is now the most recently used
        cache.put("c", sample("C")); // evicts "b"
        check(cache.get("a").has_value(), "Recently used entry survives eviction");
        check(!cache.get("b").has_value(), "Least recently used entry is evicted at capacity");
        check(cache.get("c").has_value(), "Newest entry is present");
        check(cache.size() == 2, "Cache never exceeds its capacity");
    }

    // ---- cache keys -----------------------------------------------------------
    {
        rl::RouteRequest r;
        r.fromNode = 5;
        r.toNode = 9;
        r.scenario = "normal";
        const std::string base = rl::RouteCache::makeKey(r);

        rl::RouteRequest same = r;
        same.wTime += 0.0001;  // slider jitter below the 3-decimal rounding
        check(rl::RouteCache::makeKey(same) == base, "Tiny priority jitter gives the same key");

        rl::RouteRequest w = r;
        w.wTime = 0.7;
        check(rl::RouteCache::makeKey(w) != base, "Different priorities give a different key");

        rl::RouteRequest s = r;
        s.scenario = "rain";
        check(rl::RouteCache::makeKey(s) != base, "Different scenario gives a different key");

        rl::RouteRequest n = r;
        n.toNode = 10;
        check(rl::RouteCache::makeKey(n) != base, "Different destination gives a different key");
    }

    // ---- many threads at once (Crow is multithreaded) -------------------------
    {
        rl::RouteCache cache(64, 300.0);
        std::vector<std::thread> workers;
        for (int t = 0; t < 4; ++t) {
            workers.emplace_back([&cache, t] {
                for (int i = 0; i < 2000; ++i) {
                    const std::string key = "k" + std::to_string((i + t) % 100);
                    if (!cache.get(key)) cache.put(key, sample("X"));
                }
            });
        }
        for (auto& w : workers) w.join();
        check(cache.size() <= 64, "Capacity holds under concurrent use");
        check(cache.hits() + cache.misses() == 8000, "Every concurrent lookup is counted exactly once");
    }

    std::cout << "Route cache tests: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
