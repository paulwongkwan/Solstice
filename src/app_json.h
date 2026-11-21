/**
 * @file src/app_json.h
 * @brief Helpers for editing the serialized application list.
 */
#pragma once

// standard includes
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

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
   * @brief Extract the executable directory from a command that runs a single quoted executable.
   *
   * The command must be exactly one double-quoted absolute path to an `.exe` file; commands with arguments, relative
   * paths, or other file types are rejected.
   *
   * @param command Command line from an application entry.
   * @return Parent directory of the executable, or no value when the command does not qualify.
   */
  std::optional<std::string> executable_directory(std::string_view command);

  /**
   * @brief Fill in an application entry's working directory from its executable.
   *
   * An existing non-empty `working-dir` is preserved.
   *
   * @param entry Application entry, mutated in place.
   * @return True when a working directory was written.
   */
  bool fill_working_directory(nlohmann::json &entry);

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
