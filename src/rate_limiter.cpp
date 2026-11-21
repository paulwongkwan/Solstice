/**
 * @file src/rate_limiter.cpp
 * @brief Definitions for the request rate limiter.
 */

// local includes
#include "rate_limiter.h"

namespace rate_limiter {
  limiter_t::limiter_t(std::chrono::milliseconds window_length, std::size_t max_calls) : window {window_length}, max_calls {max_calls} {
  }

  bool limiter_t::allow() {
    return allow(std::chrono::steady_clock::now());
  }

  bool limiter_t::allow(std::chrono::steady_clock::time_point now) {
    const std::scoped_lock lock {mutex};

    const auto cutoff = now - window;
    std::erase_if(calls, [cutoff](const auto &call) {
      return call <= cutoff;
    });

    if (calls.size() >= max_calls) {
      return false;
    }

    calls.push_back(now);
    return true;
  }
}  // namespace rate_limiter
