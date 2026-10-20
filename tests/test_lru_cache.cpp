#include "lru_cache.hpp"

#include <iostream>
#include <string>

int main() {
    int checks = 0;
    int failures = 0;

    auto check = [&](bool condition, const std::string& message) {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    };

    // Test 1: Empty cache
    rl::LRUCache<int, std::string> cache(2);
    check(cache.empty(), "New cache should be empty");
    check(!cache.get(1).has_value(), "Missing key should return no value");

    // Test 2: Insert and retrieve
    cache.put(1, "traffic-A");
    cache.put(2, "traffic-B");
    check(cache.get(1).value_or("") == "traffic-A",
          "Should retrieve existing value");
    check(cache.size() == 2, "Cache should contain two entries");

    // Test 3: LRU eviction
    cache.put(3, "traffic-C");
    check(!cache.contains(2), "Least recently used key should be evicted");
    check(cache.contains(1), "Recently accessed key should remain");
    check(cache.contains(3), "New key should be present");

    // Test 4: Update existing key
    cache.put(1, "updated-A");
    check(cache.get(1).value_or("") == "updated-A",
          "Existing key should be updated");

    // Test 5: Erase
    check(cache.erase(1), "Erase should succeed for existing key");
    check(!cache.contains(1), "Erased key should be absent");
    check(!cache.erase(99), "Erase should fail for missing key");

    // Test 6: Zero-capacity cache
    rl::LRUCache<int, int> zero_cache(0);
    zero_cache.put(1, 100);
    check(zero_cache.empty(), "Zero-capacity cache should stay empty");

    // Test 7: Clear
    cache.put(4, "traffic-D");
    cache.clear();
    check(cache.empty(), "Clear should remove all entries");

    std::cout << "LRU cache tests: " << checks
              << " checks, " << failures << " failures\n";

    return failures == 0 ? 0 : 1;
}
