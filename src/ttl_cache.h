/**
 * @file src/ttl_cache.h
 * @brief Small time-limited cache for expensive lookups.
 */
#pragma once

// standard includes
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace ttl_cache {
  /**
   * @brief Cache of values that expire after a fixed time and has a bounded size.
   *
   * The cache is safe to use from several HTTP worker threads. Expired entries are dropped when they are looked up or
   * when a new entry needs room.
   *
   * @tparam value_t Value stored for each key.
   */
  template<typename value_t>
  class cache_t {
  public:
    using clock = std::chrono::steady_clock;  ///< Clock used to age entries.
    using time_point = clock::time_point;  ///< Timestamp type used by the cache.

    /**
     * @brief Create a cache.
     *
     * @param time_to_live How long a stored value stays valid.
     * @param capacity Maximum number of entries kept.
     */
    cache_t(std::chrono::milliseconds time_to_live, std::size_t capacity) :
        time_to_live {time_to_live},
        capacity {capacity} {}

    /**
     * @brief Read a value at the current time.
     *
     * @param key Entry key.
     * @return Stored value, or no value when the entry is missing or expired.
     */
    std::optional<value_t> get(const std::string &key) {
      return get(key, clock::now());
    }

    /**
     * @brief Read a value at an explicit time.
     *
     * @param key Entry key.
     * @param now Time used to judge expiry.
     * @return Stored value, or no value when the entry is missing or expired.
     */
    std::optional<value_t> get(const std::string &key, time_point now) {
      const std::scoped_lock lock {mutex};

      const auto found = entries.find(key);
      if (found == entries.end()) {
        return std::nullopt;
      }

      if (found->second.expires <= now) {
        entries.erase(found);
        return std::nullopt;
      }

      return found->second.value;
    }

    /**
     * @brief Store a value at the current time.
     *
     * @param key Entry key.
     * @param value Value to store.
     */
    void put(const std::string &key, value_t value) {
      put(key, std::move(value), clock::now());
    }

    /**
     * @brief Store a value at an explicit time.
     *
     * @param key Entry key.
     * @param value Value to store.
     * @param now Time used to start the entry's lifetime.
     */
    void put(const std::string &key, value_t value, time_point now) {
      const std::scoped_lock lock {mutex};

      if (capacity == 0) {
        return;
      }

      std::erase_if(entries, [now](const auto &item) {
        return item.second.expires <= now;
      });

      if (entries.size() >= capacity && !entries.contains(key)) {
        // Drop the entry that expires first to make room for the new one.
        const auto first = std::ranges::min_element(entries, [](const auto &left, const auto &right) {
          return left.second.expires < right.second.expires;
        });
        entries.erase(first);
      }

      entries.insert_or_assign(key, entry_t {std::move(value), now + time_to_live});
    }

    /**
     * @brief Remove every entry.
     */
    void clear() {
      const std::scoped_lock lock {mutex};
      entries.clear();
    }

    /**
     * @brief Number of stored entries, including ones that have not been expired yet.
     *
     * @return Entry count.
     */
    std::size_t size() const {
      const std::scoped_lock lock {mutex};
      return entries.size();
    }

  private:
    /**
     * @brief A stored value and the time it stops being valid.
     */
    struct entry_t {
      value_t value;  ///< Cached value.
      time_point expires;  ///< Time after which the value is stale.
    };

    std::chrono::milliseconds time_to_live;  ///< Lifetime of a stored value.
    std::size_t capacity;  ///< Maximum number of entries.
    mutable std::mutex mutex;  ///< Guards `entries` across threads.
    std::unordered_map<std::string, entry_t> entries;  ///< Stored values by key.
  };
}  // namespace ttl_cache
