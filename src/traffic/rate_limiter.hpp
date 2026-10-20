#ifndef ROUTELENS_RATE_LIMITER_HPP
#define ROUTELENS_RATE_LIMITER_HPP

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>

namespace rl {

class RateLimiter {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = Clock::duration;

private:
    std::size_t max_requests_;
    Duration window_;
    std::deque<Clock::time_point> requests_;
    mutable std::mutex mutex_;

public:
    RateLimiter(std::size_t max_requests, Duration window)
        : max_requests_(max_requests), window_(window) {}

    bool allow_request() {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = Clock::now();

        while (!requests_.empty() &&
               now - requests_.front() >= window_) {
            requests_.pop_front();
        }

        if (max_requests_ == 0 || window_ <= Duration::zero() ||
            requests_.size() >= max_requests_) {
            return false;
        }

        requests_.push_back(now);
        return true;
    }

    std::size_t current_count() {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = Clock::now();

        while (!requests_.empty() &&
               now - requests_.front() >= window_) {
            requests_.pop_front();
        }

        return requests_.size();
    }
};

}  // namespace rl

#endif
