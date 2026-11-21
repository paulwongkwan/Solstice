/**
 * @file src/steam_library.h
 * @brief Discovery of Steam libraries and installed games.
 */
#pragma once

// standard includes
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace steam_library {
  /**
   * @brief One entry of a Valve KeyValues document.
   */
  struct vdf_node_t {
    std::string value;  ///< Value of a leaf entry.
    std::map<std::string, vdf_node_t> children;  ///< Nested entries of a section.
    bool section {};  ///< Whether the entry opened a brace block, even when the block is empty.
  };

  /**
   * @brief An installed Steam application.
   */
  struct steam_app_t {
    std::string app_id;  ///< Steam application ID.
    std::string name;  ///< Title reported by Steam.
    std::string install_dir;  ///< Directory name below `steamapps/common`.
  };

  /**
   * @brief Parse a Valve KeyValues document.
   *
   * @param text Document contents.
   * @return Top-level entries, or no value when the document is malformed.
   */
  std::optional<std::map<std::string, vdf_node_t>> parse_vdf(std::string_view text);

  /**
   * @brief Read the library folders declared by `libraryfolders.vdf`.
   *
   * Both the current format, where each library is a section with a `path` entry, and the older format, where the
   * library path is the value of a numbered entry, are recognized.
   *
   * @param document Parsed `libraryfolders.vdf`.
   * @return Library root folders, in file order, without duplicates.
   */
  std::vector<std::filesystem::path> library_paths(const std::map<std::string, vdf_node_t> &document);

  /**
   * @brief Parse an `appmanifest_<appid>.acf` document.
   *
   * @param text Document contents.
   * @return Application details, or no value when the manifest is malformed or the application is not installed.
   */
  std::optional<steam_app_t> parse_app_manifest(std::string_view text);

  /**
   * @brief Locate the Steam installation directory.
   *
   * Checks the `HKCU\\Software\\Valve\\Steam` registry keys on Windows, then the usual installation folders.
   *
   * @return Steam root folder, or no value when Steam is not installed.
   */
  std::optional<std::filesystem::path> find_steam_root();

  /**
   * @brief List the Steam applications that are fully installed in every library.
   *
   * @param steam_root Steam root folder.
   * @return Installed applications ordered by name, with duplicates removed.
   */
  std::vector<steam_app_t> scan_installed(const std::filesystem::path &steam_root);

  /**
   * @brief Build the command that asks Steam to launch an application.
   *
   * @param steam_exe Steam executable.
   * @param app_id Steam application ID.
   * @return Quoted command line.
   */
  std::string launch_command(const std::filesystem::path &steam_exe, std::string_view app_id);
}  // namespace steam_library
