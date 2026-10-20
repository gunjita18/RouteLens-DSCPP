#include "rate_limiter.hpp"

#include <chrono>
#include <iostream>
#include <thread>

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

    using namespace std::chrono_literals;

    rl::RateLimiter limiter(2, 200ms);

    check(limiter.allow_request(), "First request should be allowed");
    check(limiter.allow_request(), "Second request should be allowed");
    check(!limiter.allow_request(), "Third request should be blocked");
    check(limiter.current_count() == 2, "Count should be two");

    std::this_thread::sleep_for(250ms);

    check(limiter.allow_request(), "Request should be allowed after window expires");

    rl::RateLimiter zero_limit(0, 1s);
    check(!zero_limit.allow_request(), "Zero request limit should block requests");

    rl::RateLimiter zero_window(2, 0ms);
    check(!zero_window.allow_request(), "Zero time window should block requests");

    std::cout << "Rate limiter tests: " << checks
              << " checks, " << failures << " failures\n";

    return failures == 0 ? 0 : 1;
}
