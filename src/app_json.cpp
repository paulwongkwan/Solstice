/**
 * @file src/app_json.cpp
 * @brief Helpers for editing the serialized application list.
 */

// local includes
#include "app_json.h"

// standard includes
#include <algorithm>
#include <cctype>
#include <string>

namespace app_json {
  bool has_valid_name(const nlohmann::json &entry) {
    if (!entry.is_object()) {
      return false;
    }

    const auto name = entry.find("name");
    if (name == entry.end() || !name->is_string()) {
      return false;
    }

    const auto &value = name->get_ref<const std::string &>();
    return std::ranges::any_of(value, [](unsigned char character) {
      return std::isspace(character) == 0;
    });
  }

  std::size_t append(nlohmann::json &file_tree, const nlohmann::json &entries) {
    if (!file_tree.is_object() || !entries.is_array()) {
      return 0;
    }

    auto &apps = file_tree["apps"];
    if (!apps.is_array()) {
      apps = nlohmann::json::array();
    }

    std::size_t added = 0;
    for (const auto &entry : entries) {
      if (!has_valid_name(entry)) {
        continue;
      }
      apps.push_back(entry);
      ++added;
    }

    std::stable_sort(apps.begin(), apps.end(), [](const nlohmann::json &left, const nlohmann::json &right) {
      const auto left_name = left.contains("name") && left["name"].is_string() ? left["name"].get<std::string>() : std::string {};
      const auto right_name = right.contains("name") && right["name"].is_string() ? right["name"].get<std::string>() : std::string {};
      return left_name < right_name;
    });

    return added;
  }
}  // namespace app_json
