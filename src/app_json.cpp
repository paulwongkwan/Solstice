/**
 * @file src/app_json.cpp
 * @brief Helpers for editing the serialized application list.
 */

// local includes
#include "app_json.h"

// standard includes
#include <algorithm>
#include <cctype>
#include <filesystem>
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

  std::optional<std::string> executable_directory(std::string_view command) {
    const auto first = command.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos || command[first] != '"') {
      return std::nullopt;
    }

    const auto closing = command.find('"', first + 1);
    if (closing == std::string_view::npos) {
      return std::nullopt;
    }

    // Arguments after the executable would make the working directory ambiguous.
    if (command.find_first_not_of(" \t\r\n", closing + 1) != std::string_view::npos) {
      return std::nullopt;
    }

    const std::filesystem::path executable {std::string {command.substr(first + 1, closing - first - 1)}};
    auto extension = executable.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    });
    if (!executable.is_absolute() || extension != ".exe") {
      return std::nullopt;
    }

    const auto directory = executable.parent_path().string();
    return directory.empty() ? std::nullopt : std::optional<std::string> {directory};
  }

  bool fill_working_directory(nlohmann::json &entry) {
    if (!entry.is_object()) {
      return false;
    }

    const auto working_dir = entry.find("working-dir");
    if (working_dir != entry.end() && working_dir->is_string() &&
        working_dir->get_ref<const std::string &>().find_first_not_of(" \t\r\n") != std::string::npos) {
      return false;
    }

    const auto cmd = entry.find("cmd");
    if (cmd == entry.end() || !cmd->is_string()) {
      return false;
    }

    const auto directory = executable_directory(cmd->get_ref<const std::string &>());
    if (!directory) {
      return false;
    }

    entry["working-dir"] = *directory;
    return true;
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
