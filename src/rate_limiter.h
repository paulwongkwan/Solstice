/**
 * @file src/rate_limiter.h
 * @brief Rate limiting for endpoints that run slow or networked work.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstddef>
#include <mutex>
#include <vector>

namespace rate_limiter {
  /**
   * @brief Sliding-window limiter that allows a fixed number of calls per interval.
   *
   * Calls are counted over a window that moves with the clock, so a burst of calls is allowed at once and calls become
   * available again as older ones leave the window. Instances are safe to use from several HTTP worker threads.
   */
  class limiter_t {
  public:
    /**
     * @brief Create a limiter.
     *
     * @param window_length Length of the sliding window.
     * @param max_calls Maximum number of calls allowed inside the window.
     */
    explicit limiter_t(std::chrono::milliseconds window_length, std::size_t max_calls = 1);

    /**
     * @brief Try to perform a call at the current time.
     *
     * @return True when the call is allowed.
     */
    bool allow();

    /**
     * @brief Try to perform a call at an explicit time.
     *
     * @param now Time of the call.
     * @return True when the call is allowed.
     */
    bool allow(std::chrono::steady_clock::time_point now);

  private:
    std::chrono::milliseconds window;  ///< Length of the sliding window.
    std::size_t max_calls;  ///< Number of calls allowed inside the window.
    std::vector<std::chrono::steady_clock::time_point> calls;  ///< Accepted calls that are still inside the window.
    std::mutex mutex;  ///< Guards `calls` across threads.
  };
}  // namespace rate_limiter
