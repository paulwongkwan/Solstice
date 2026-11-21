/**
 * @file src/game_scanner.cpp
 * @brief Discovery of game executables inside a folder.
 */

// local includes
#include "game_scanner.h"
#include "logging.h"

// standard includes
#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>

// lib includes
#include <Windows.h>

namespace game_scanner {
  using namespace std::literals;

  namespace {
    /**
     * @brief Folder names that never contain the game the user wants to launch.
     */
    constexpr std::array<std::string_view, 10> skipped_directories {
      "redist",
      "redistributables",
      "_commonredist",
      "directx",
      "dotnet",
      "vcredist",
      "installers",
      "uninstall",
      "$recycle.bin",
      "system volume information",
    };

    /**
     * @brief File name fragments that identify support tooling rather than a game.
     */
    constexpr std::array<std::string_view, 12> skipped_fragments {
      "unins",
      "uninstall",
      "setup",
      "installer",
      "vcredist",
      "redist",
      "dxsetup",
      "dxwebsetup",
      "dotnetfx",
      "crashreport",
      "crashpad",
      "updater",
    };

    /**
     * @brief Lowercase a string using the C locale.
     *
     * @param value String to lowercase.
     * @return Lowercased copy.
     */
    std::string lowercase(std::string_view value) {
      std::string result {value};
      std::ranges::transform(result, result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return result;
    }

    /**
     * @brief Whether a folder should be skipped entirely.
     *
     * @param name Folder name.
     * @return True when the folder holds support files.
     */
    bool is_skipped_directory(std::string_view name) {
      const auto lower = lowercase(name);
      return std::ranges::any_of(skipped_directories, [&lower](std::string_view skipped) {
        return lower == skipped;
      });
    }
  }  // namespace

  bool is_game_executable(std::string_view filename) {
    const auto lower = lowercase(filename);
    if (!lower.ends_with(".exe")) {
      return false;
    }

    const auto stem = lower.substr(0, lower.size() - 4);
    if (stem.empty()) {
      return false;
    }

    return std::ranges::none_of(skipped_fragments, [&lower](std::string_view fragment) {
      return lower.find(fragment) != std::string::npos;
    });
  }

  bool is_local_path(const std::filesystem::path &path) {
    if (path.empty() || !path.is_absolute()) {
      return false;
    }

    // UNC shares, including the extended-length and device forms, live on another machine.
    const auto text = path.string();
    if (text.starts_with("\\\\") || text.starts_with("//")) {
      return false;
    }

    const auto root = path.root_name().wstring();
    if (root.size() == 2 && root[1] == L':') {
      const std::wstring drive {root + L"\\"};
      if (GetDriveTypeW(drive.c_str()) == DRIVE_REMOTE) {
        return false;
      }
    }

    return true;
  }

  std::string display_name(std::string_view filename) {
    std::string name {filename};
    if (const auto dot = name.find_last_of('.'); dot != std::string::npos) {
      name.erase(dot);
    }

    for (auto &character : name) {
      if (character == '_' || character == '.') {
        character = ' ';
      }
    }

    // Collapse runs of separators and trim the edges.
    std::string collapsed;
    collapsed.reserve(name.size());
    for (const auto character : name) {
      const auto space = std::isspace(static_cast<unsigned char>(character)) != 0;
      if (space && (collapsed.empty() || collapsed.back() == ' ')) {
        continue;
      }
      collapsed.push_back(character);
    }
    while (!collapsed.empty() && collapsed.back() == ' ') {
      collapsed.pop_back();
    }

    const auto has_upper = std::ranges::any_of(collapsed, [](unsigned char c) {
      return std::isupper(c) != 0;
    });
    if (!has_upper) {
      bool capitalize = true;
      for (auto &character : collapsed) {
        const auto letter = std::isalpha(static_cast<unsigned char>(character)) != 0;
        if (letter && capitalize) {
          character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
          capitalize = false;
        } else if (character == ' ') {
          capitalize = true;
        }
      }
    }

    return collapsed;
  }

  std::vector<scanned_game_t> scan(const std::filesystem::path &root, std::size_t max_depth, std::size_t max_results) {
    std::vector<scanned_game_t> games;

    std::error_code error;
    if (root.empty() || !std::filesystem::is_directory(root, error)) {
      return games;
    }

    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator iterator {root, options, error};
    if (error) {
      BOOST_LOG(warning) << "Couldn't scan ["sv << root.string() << "]: "sv << error.message();
      return games;
    }

    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end && games.size() < max_results; iterator.increment(error)) {
      if (error) {
        error.clear();
        break;
      }

      if (iterator.depth() >= static_cast<int>(max_depth)) {
        iterator.disable_recursion_pending();
      }

      const auto &entry = *iterator;
      const auto filename = entry.path().filename().string();

      if (entry.is_directory(error)) {
        if (is_skipped_directory(filename)) {
          iterator.disable_recursion_pending();
        }
        continue;
      }

      if (!entry.is_regular_file(error) || !is_game_executable(filename)) {
        continue;
      }

      games.push_back({display_name(filename), std::filesystem::absolute(entry.path(), error).string()});
    }

    std::ranges::sort(games, [](const scanned_game_t &left, const scanned_game_t &right) {
      return left.name < right.name;
    });
    const auto duplicates = std::ranges::unique(games, [](const scanned_game_t &left, const scanned_game_t &right) {
      return left.path == right.path;
    });
    games.erase(duplicates.begin(), duplicates.end());

    return games;
  }
}  // namespace game_scanner
