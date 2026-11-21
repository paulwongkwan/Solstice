/**
 * @file src/app_json.h
 * @brief Helpers for editing the serialized application list.
 */
#pragma once

// standard includes
#include <cstddef>

// lib includes
#include <nlohmann/json.hpp>

namespace app_json {
  /**
   * @brief Whether an application entry has the fields required to create it.
   *
   * @param entry Application entry from a save request.
   * @return True when the entry is an object with a non-empty string name.
   */
  bool has_valid_name(const nlohmann::json &entry);

  /**
   * @brief Append application entries to an application list document and sort them by name.
   *
   * The `apps` member is created when it is missing or is not an array. Entries that are not objects, or that have no
   * usable name, are skipped.
   *
   * @param file_tree Application list document, mutated in place.
   * @param entries Application entries to append.
   * @return Number of entries appended.
   */
  std::size_t append(nlohmann::json &file_tree, const nlohmann::json &entries);
}  // namespace app_json
