/**
 * @file src/game_scanner.h
 * @brief Discovery of game executables inside a folder.
 */
#pragma once

// standard includes
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace game_scanner {
  /**
   * @brief A game executable discovered on disk.
   */
  struct scanned_game_t {
    std::string name;  ///< Display name derived from the executable's file name.
    std::string path;  ///< Absolute path to the executable.
  };

  /**
   * @brief Whether a file name looks like a game executable rather than support tooling.
   *
   * Installers, redistributables, crash handlers, and uninstallers are rejected.
   *
   * @param filename File name, including its extension.
   * @return True when the file should be offered as a game.
   */
  bool is_game_executable(std::string_view filename);

  /**
   * @brief Whether a path can be scanned without waiting on the network.
   *
   * Only absolute paths on a local drive qualify. Relative paths, UNC shares, device paths, and mapped network
   * drives are rejected, because walking them can block for minutes when the share is unavailable.
   *
   * @param path Folder path supplied by a client.
   * @return True when the path is absolute and stored on a local drive.
   */
  bool is_local_path(const std::filesystem::path &path);

  /**
   * @brief Derive a display name from an executable file name.
   *
   * @param filename File name, with or without its extension.
   * @return Readable name with separators collapsed into spaces.
   */
  std::string display_name(std::string_view filename);

  /**
   * @brief Recursively scan a folder for game executables.
   *
   * Symbolic links are not followed, support folders such as `redist` and `_CommonRedist` are skipped, and each
   * executable is only reported once.
   *
   * @param root Folder to scan.
   * @param max_depth Maximum directory depth below the root.
   * @param max_results Maximum number of games to return.
   * @return Games ordered by display name; empty when the folder is missing or contains no games.
   */
  std::vector<scanned_game_t> scan(const std::filesystem::path &root, std::size_t max_depth = 4, std::size_t max_results = 500);
}  // namespace game_scanner
