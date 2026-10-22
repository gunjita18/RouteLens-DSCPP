// test_tomtom_cache.cpp — the TomTom segment cache is bounded (Aayush's LRUCache):
// least recently used segments are evicted, hits refresh recency, refetched roads
// keep their id, no-coverage points are capped. Offline, fake TomTom.
#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>

#include "../src/traffic/tomtom_client.hpp"

namespace fs = std::filesystem;
using namespace rl;

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #c "\n"; } } while (0)

// A 110 m north-south road through the query point's longitude (one road per longitude),
// or, if far is true, a road 0.5 deg (~48 km) to the east of the point (no coverage).
static std::string roadJson(double lat, double lon, bool far = false) {
    double rlon = far ? lon + 0.5 : lon;
    std::ostringstream o;
    o.precision(10);
    o << "{\"flowSegmentData\":{\"frc\":\"FRC2\",\"currentSpeed\":30,\"freeFlowSpeed\":40,\"confidence\":0.9,"
      << "\"roadClosure\":false,\"coordinates\":{\"coordinate\":["
      << "{\"latitude\":" << lat - 0.0005 << ",\"longitude\":" << rlon << "},"
      << "{\"latitude\":" << lat + 0.0005 << ",\"longitude\":" << rlon << "}]}}}";
    return o.str();
}

static double urlNumber(const std::string& url, const std::string& key, int which) {
    size_t p = url.find(key) + key.size();
    std::string coords = url.substr(p, url.find('&', p) - p);
    size_t c = coords.find(',');
    return std::stod(which == 0 ? coords.substr(0, c) : coords.substr(c + 1));
}

struct Fake {
    int calls = 0;
    bool far = false;
    double clock = 1000.0;
};

static TomTomClient makeClient(Fake& f, int maxSegments, const std::string& usage) {
    TomTomOptions o;
    o.apiKey = "K";
    o.maxCachedSegments = maxSegments;
    o.usageFile = usage;
    o.dailyCap = 2000;
    return TomTomClient(
        o,
        [&f](const std::string& url) {
            f.calls++;
            HttpResponse r;
            r.status = 200;
            r.body = roadJson(urlNumber(url, "point=", 0), urlNumber(url, "point=", 1), f.far);
            return r;
        },
        [&f] { return f.clock; }, [&f](double s) { f.clock += s; }, [] { return std::string("2026-10-22"); });
}

int main() {
    fs::path dir = fs::temp_directory_path() / "routelens_test_tomtom_cache";
    fs::remove_all(dir);
    std::string usage = (dir / "usage.txt").string();
    const double lat = 30.30;

    // ---- LRU order with room for 3 roads
    {
        Fake f;
        TomTomClient c = makeClient(f, 3, usage);
        FetchResult a = c.fetch({ lat, 78.00 });
        FetchResult b = c.fetch({ lat, 78.01 });
        FetchResult d = c.fetch({ lat, 78.02 });
        CHECK(a.status == FetchStatus::Ok && b.status == FetchStatus::Ok && d.status == FetchStatus::Ok);
        CHECK(f.calls == 3 && c.cachedSegments() == 3);
        CHECK(std::fabs(c.segment(b.segmentIndex).shape[0].lon - 78.01) < 1e-9);

        // A hit on road 78.00 makes it recently used; then a 4th road evicts 78.01 (least recent).
        CHECK(c.fetch({ lat + 0.0002, 78.00 }).status == FetchStatus::CachedOk && f.calls == 3);
        CHECK(c.fetch({ lat, 78.03 }).status == FetchStatus::Ok && f.calls == 4);
        CHECK(c.cachedSegments() == 3);
        CHECK(c.fetch({ lat, 78.00 }).status == FetchStatus::CachedOk && f.calls == 4);   // kept
        CHECK(c.fetch({ lat, 78.01 }).status == FetchStatus::Ok && f.calls == 5);         // was evicted
        CHECK(c.cachedSegments() == 3);
        CHECK(c.fetch({ lat, 78.02 }).status == FetchStatus::Ok && f.calls == 6);         // evicted by 78.01
        CHECK(c.cachedSegments() == 3);
    }

    // ---- refetching the same road after expiry keeps its id and does not grow the cache
    {
        fs::remove_all(dir);
        Fake f;
        TomTomClient c = makeClient(f, 5, usage);
        FetchResult first = c.fetch({ lat, 78.05 });
        f.clock += 301;   // past the 5-minute expiry
        FetchResult again = c.fetch({ lat, 78.05 });
        CHECK(again.status == FetchStatus::Ok && f.calls == 2);
        CHECK(again.segmentIndex == first.segmentIndex && c.cachedSegments() == 1);
    }

    // ---- bounded under load: 1000 different roads through a cache of 50
    {
        fs::remove_all(dir);
        Fake f;
        TomTomClient c = makeClient(f, 50, usage);
        int maxSeen = 0, ok = 0;
        for (int i = 0; i < 1000; i++) {
            FetchResult r = c.fetch({ lat, 77.95 + i * 0.001 });   // ~96 m apart: all different roads
            if (r.status == FetchStatus::Ok) ok++;
            maxSeen = std::max(maxSeen, c.cachedSegments());
        }
        CHECK(ok == 1000 && f.calls == 1000);
        CHECK(maxSeen == 50 && c.cachedSegments() == 50);
    }

    // ---- no-coverage points are capped too, and expired ones are dropped
    {
        fs::remove_all(dir);
        Fake f;
        f.far = true;
        TomTomClient c = makeClient(f, 10, usage);
        for (int i = 0; i < 25; i++) {
            FetchResult r = c.fetch({ lat, 78.0 + i * 0.001 });
            CHECK(r.status == FetchStatus::NoCoverage);
        }
        CHECK(c.cachedMisses() <= 10);
        f.clock += 301;
        c.fetch({ lat, 79.0 });   // adding a new miss drops all expired ones
        CHECK(c.cachedMisses() == 1);
    }

    // ---- a limit of 0 is treated as 1 (no crash, still caches the last road)
    {
        fs::remove_all(dir);
        Fake f;
        TomTomClient c = makeClient(f, 0, usage);
        CHECK(c.fetch({ lat, 78.00 }).status == FetchStatus::Ok);
        CHECK(c.fetch({ lat, 78.00 }).status == FetchStatus::CachedOk && f.calls == 1);
        CHECK(c.fetch({ lat, 78.01 }).status == FetchStatus::Ok && c.cachedSegments() == 1);
    }

    fs::remove_all(dir);
    std::cout << "test_tomtom_cache: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
