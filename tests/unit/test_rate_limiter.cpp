/**
 * @file tests/unit/test_rate_limiter.cpp
 * @brief Tests for the request rate limiter.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <chrono>

// local includes
#include <src/rate_limiter.h>

namespace {
  using steady_time = std::chrono::steady_clock::time_point;  ///< Timestamps used by the tests.
}  // namespace

TEST(RateLimiterTest, AllowsABurstInsideTheWindow) {
  rate_limiter::limiter_t limiter {std::chrono::milliseconds {1000}, 2};
  const steady_time start {};

  EXPECT_TRUE(limiter.allow(start));
  EXPECT_TRUE(limiter.allow(start + std::chrono::milliseconds {100}));
}

TEST(RateLimiterTest, RejectsCallsBeyondTheBurst) {
  rate_limiter::limiter_t limiter {std::chrono::milliseconds {1000}, 2};
  const steady_time start {};

  ASSERT_TRUE(limiter.allow(start));
  ASSERT_TRUE(limiter.allow(start + std::chrono::milliseconds {100}));

  EXPECT_FALSE(limiter.allow(start + std::chrono::milliseconds {200}));
  EXPECT_FALSE(limiter.allow(start + std::chrono::milliseconds {900}));
}

TEST(RateLimiterTest, AllowsCallsAgainOnceTheWindowMovesOn) {
  rate_limiter::limiter_t limiter {std::chrono::milliseconds {1000}, 2};
  const steady_time start {};

  ASSERT_TRUE(limiter.allow(start));
  ASSERT_TRUE(limiter.allow(start + std::chrono::milliseconds {100}));
  ASSERT_FALSE(limiter.allow(start + std::chrono::milliseconds {500}));

  EXPECT_TRUE(limiter.allow(start + std::chrono::milliseconds {1000}));
  EXPECT_FALSE(limiter.allow(start + std::chrono::milliseconds {1050}));
  EXPECT_TRUE(limiter.allow(start + std::chrono::milliseconds {1100}));
}

TEST(RateLimiterTest, RejectsEverythingWhenNoCallsAreAllowed) {
  rate_limiter::limiter_t limiter {std::chrono::milliseconds {1000}, 0};
  const steady_time start {};

  EXPECT_FALSE(limiter.allow(start));
  EXPECT_FALSE(limiter.allow(start + std::chrono::milliseconds {5000}));
}

TEST(RateLimiterTest, CountsCallsMadeWithoutAnExplicitTime) {
  rate_limiter::limiter_t limiter {std::chrono::hours {1}};

  EXPECT_TRUE(limiter.allow());
  EXPECT_FALSE(limiter.allow());
}
