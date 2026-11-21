/**
 * @file tests/unit/test_ttl_cache.cpp
 * @brief Tests for the expiring bounded cache.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <chrono>
#include <string>

// local includes
#include <src/ttl_cache.h>

namespace {
  using cache_t = ttl_cache::cache_t<std::string>;  ///< Cache type used by the tests.
  using timestamp = cache_t::time_point;  ///< Timestamps used by the tests.
  using std::chrono::milliseconds;  ///< Short unit used by the tests.
}  // namespace

TEST(TtlCacheTest, ReturnsAValueUntilItExpires) {
  cache_t cache {milliseconds {1000}, 4};
  const timestamp start {};

  cache.put("hades", "cover", start);

  ASSERT_TRUE(cache.get("hades", start).has_value());
  EXPECT_EQ(*cache.get("hades", start), "cover");
  EXPECT_TRUE(cache.get("hades", start + milliseconds {999}).has_value());
  EXPECT_FALSE(cache.get("hades", start + milliseconds {1000}).has_value());
}

TEST(TtlCacheTest, RemovesAnExpiredEntryWhenItIsRead) {
  cache_t cache {milliseconds {1000}, 4};
  const timestamp start {};

  cache.put("hades", "cover", start);
  ASSERT_EQ(cache.size(), 1u);

  EXPECT_FALSE(cache.get("hades", start + milliseconds {1000}).has_value());
  EXPECT_EQ(cache.size(), 0u);
}

TEST(TtlCacheTest, ReturnsNothingForUnknownKeys) {
  cache_t cache {milliseconds {1000}, 4};

  EXPECT_FALSE(cache.get("missing", timestamp {}).has_value());
}

TEST(TtlCacheTest, EvictsTheEntryThatExpiresFirstWhenFull) {
  cache_t cache {milliseconds {1000}, 2};
  const timestamp start {};

  cache.put("first", "a", start);
  cache.put("second", "b", start + milliseconds {100});
  cache.put("third", "c", start + milliseconds {200});

  EXPECT_EQ(cache.size(), 2u);
  EXPECT_FALSE(cache.get("first", start + milliseconds {200}).has_value());
  EXPECT_TRUE(cache.get("second", start + milliseconds {200}).has_value());
  EXPECT_TRUE(cache.get("third", start + milliseconds {200}).has_value());
}

TEST(TtlCacheTest, ReplacesAnExistingKeyWithoutEvictingAnother) {
  cache_t cache {milliseconds {1000}, 2};
  const timestamp start {};

  cache.put("first", "a", start);
  cache.put("second", "b", start);
  cache.put("first", "updated", start + milliseconds {100});

  ASSERT_EQ(cache.size(), 2u);
  EXPECT_EQ(*cache.get("first", start + milliseconds {100}), "updated");
  EXPECT_EQ(*cache.get("second", start + milliseconds {100}), "b");
}

TEST(TtlCacheTest, DropsExpiredEntriesBeforeEvictingALiveOne) {
  cache_t cache {milliseconds {1000}, 2};
  const timestamp start {};

  cache.put("expired", "a", start);
  cache.put("live", "b", start + milliseconds {800});
  cache.put("new", "c", start + milliseconds {1500});

  ASSERT_EQ(cache.size(), 2u);
  EXPECT_FALSE(cache.get("expired", start + milliseconds {1500}).has_value());
  EXPECT_TRUE(cache.get("live", start + milliseconds {1500}).has_value());
  EXPECT_TRUE(cache.get("new", start + milliseconds {1500}).has_value());
}

TEST(TtlCacheTest, IgnoresWritesWhenItCannotHoldEntries) {
  cache_t cache {milliseconds {1000}, 0};

  cache.put("hades", "cover", timestamp {});

  EXPECT_EQ(cache.size(), 0u);
  EXPECT_FALSE(cache.get("hades", timestamp {}).has_value());
}

TEST(TtlCacheTest, ClearsEveryEntry) {
  cache_t cache {milliseconds {1000}, 4};

  cache.put("first", "a", timestamp {});
  cache.put("second", "b", timestamp {});
  ASSERT_EQ(cache.size(), 2u);

  cache.clear();

  EXPECT_EQ(cache.size(), 0u);
  EXPECT_FALSE(cache.get("first", timestamp {}).has_value());
}

TEST(TtlCacheTest, UsesTheCurrentTimeForPlainReadsAndWrites) {
  cache_t cache {std::chrono::hours {1}, 1};

  cache.put("hades", "cover");

  ASSERT_TRUE(cache.get("hades").has_value());
  EXPECT_EQ(*cache.get("hades"), "cover");
}
