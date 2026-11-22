#ifndef ROUTELENS_TRAFFIC_DATA_HPP
#define ROUTELENS_TRAFFIC_DATA_HPP

#include <string>

// What the website shows as "Traffic data: <time>" (Phase III design, section 6).
//
// The traffic snapshot (traffic.csv, written by preprocess/fetch_traffic.py)
// carries the time it was fetched (# fetched_at = 2026-11-10 09:30). The
// server loads it with SnapshotProvider (src/oop/traffic_provider.hpp) and
// says "snapshot" only when routing really uses it; otherwise "estimated"
// (free-flow speeds).
namespace rl {

struct TrafficDataInfo {
    bool snapshotFound = false;  // a snapshot with data for at least one road loaded
    bool usedByRouting = false;  // routes are computed with the snapshot
    std::string fetchedAt;       // e.g. "2026-11-10 09:30"; empty if unknown

    // "snapshot" or "estimated" (free-flow speeds).
    const char* source() const {
        return usedByRouting && snapshotFound ? "snapshot" : "estimated";
    }
};

}  // namespace rl

#endif
