// Tests for the "Traffic data: <time>" label rule (src/server/traffic_data.hpp).
// Reading the snapshot file itself is tested with the C reader
// (tests/test_traffic_reader.c) and SnapshotProvider (tests/test_traffic_provider.cpp).
#include "traffic_data.hpp"

#include <iostream>
#include <string>

namespace {

int checks = 0;
int failures = 0;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

}  // namespace

int main() {
    // The label only says "snapshot" when routing really uses a loaded snapshot.
    rl::TrafficDataInfo info;
    check(std::string(info.source()) == "estimated", "default is estimated");
    info.snapshotFound = true;
    info.fetchedAt = "2026-11-10 09:30";
    check(std::string(info.source()) == "estimated", "snapshot loaded but unused: estimated");
    info.usedByRouting = true;
    check(std::string(info.source()) == "snapshot", "loaded and used by routing: snapshot");
    info.snapshotFound = false;
    check(std::string(info.source()) == "estimated", "used but nothing loaded: estimated");

    std::cout << "Traffic data label tests: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
