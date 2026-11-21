/**
 * @file src/steam_library.cpp
 * @brief Discovery of Steam libraries and installed games.
 */

// local includes
#include "steam_library.h"
#include "logging.h"

// standard includes
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
  // platform includes
  #include <Windows.h>
#endif

namespace steam_library {
  using namespace std::literals;

  namespace {
    /**
     * @brief Value of the Steam state flag that marks an application as fully installed.
     */
    constexpr int state_fully_installed = 4;

    /**
     * @brief Find an entry below a section, ignoring key case.
     *
     * @param document Document or section to search.
     * @param key Entry name.
     * @return Entry, or nullptr when it is missing.
     */
    const vdf_node_t *find(const std::map<std::string, vdf_node_t> &document, std::string_view key) {
      for (const auto &[name, node] : document) {
        if (name.size() == key.size() && std::equal(name.begin(), name.end(), key.begin(), key.end(), [](unsigned char a, unsigned char b) {
              return std::tolower(a) == std::tolower(b);
            })) {
          return &node;
        }
      }
      return nullptr;
    }

    /**
     * @brief Tokenizer for Valve KeyValues documents.
     */
    class tokenizer_t {
    public:
      explicit tokenizer_t(std::string_view text):
          text_ {text} {}

      /**
       * @brief Read the next token.
       *
       * @param token Receives the token text.
       * @param brace Receives the brace character when the token is `{` or `}`.
       * @return True when a token was read.
       */
      bool next(std::string &token, char &brace) {
        skip_space();
        if (position_ >= text_.size()) {
          return false;
        }

        const auto character = text_[position_];
        if (character == '{' || character == '}') {
          brace = character;
          token.clear();
          ++position_;
          return true;
        }

        brace = 0;
        if (character == '"') {
          ++position_;
          while (position_ < text_.size() && text_[position_] != '"') {
            if (text_[position_] == '\\' && position_ + 1 < text_.size()) {
              ++position_;
            }
            token.push_back(text_[position_++]);
          }
          if (position_ < text_.size()) {
            ++position_;
          }
          return true;
        }

        while (position_ < text_.size()) {
          const auto current = text_[position_];
          if (std::isspace(static_cast<unsigned char>(current)) != 0 || current == '{' || current == '}' || current == '"') {
            break;
          }
          token.push_back(current);
          ++position_;
        }
        return !token.empty();
      }

    private:
      /**
       * @brief Skip whitespace and line comments.
       */
      void skip_space() {
        while (position_ < text_.size()) {
          const auto current = text_[position_];
          if (std::isspace(static_cast<unsigned char>(current)) != 0) {
            ++position_;
          } else if (current == '/' && position_ + 1 < text_.size() && text_[position_ + 1] == '/') {
            while (position_ < text_.size() && text_[position_] != '\n') {
              ++position_;
            }
          } else {
            break;
          }
        }
      }

      std::string_view text_;  ///< Document contents.
      std::size_t position_ {};  ///< Current read offset.
    };

    /**
     * @brief Parse the entries of a section.
     *
     * @param tokenizer Token source.
     * @param document Destination entries.
     * @param nested Whether a closing brace has to terminate the section.
     * @return True when the section terminated correctly.
     */
    bool parse_entries(tokenizer_t &tokenizer, std::map<std::string, vdf_node_t> &document, bool nested) {
      while (true) {
        std::string key;
        char brace = 0;
        if (!tokenizer.next(key, brace)) {
          return !nested;
        }

        if (brace == '}') {
          return nested;
        }
        if (brace == '{') {
          return false;
        }

        std::string value;
        char next_brace = 0;
        if (!tokenizer.next(value, next_brace)) {
          document[key].value = {};
          return !nested;
        }

        auto &node = document[key];
        if (next_brace == '{') {
          node.section = true;
          if (!parse_entries(tokenizer, node.children, true)) {
            return false;
          }
        } else if (next_brace == '}') {
          node.value = value;
          return nested;
        } else {
          node.value = value;
        }
      }
    }
  }  // namespace

  std::optional<std::map<std::string, vdf_node_t>> parse_vdf(std::string_view text) {
    tokenizer_t tokenizer {text};

    std::string name;
    char brace = 0;
    if (!tokenizer.next(name, brace) || brace != 0) {
      return std::nullopt;
    }

    std::string opening;
    if (!tokenizer.next(opening, brace) || brace != '{') {
      return std::nullopt;
    }

    std::map<std::string, vdf_node_t> document;
    auto &node = document[name];
    node.section = true;
    if (!parse_entries(tokenizer, node.children, true)) {
      return std::nullopt;
    }

    return document;
  }

  std::vector<std::filesystem::path> library_paths(const std::map<std::string, vdf_node_t> &document) {
    std::vector<std::filesystem::path> paths;
    const auto *libraries = find(document, "libraryfolders");
    if (!libraries) {
      return paths;
    }

    for (const auto &[key, node] : libraries->children) {
      if (node.section) {
        if (const auto *path = find(node.children, "path"); path && !path->value.empty()) {
          paths.emplace_back(path->value);
        }
      } else if (!node.value.empty()) {
        // Older clients stored the library path directly as the value of a numbered entry.
        const bool numbered = std::ranges::all_of(key, [](unsigned char character) {
          return std::isdigit(character) != 0;
        });
        if (numbered && std::filesystem::path {node.value}.is_absolute()) {
          paths.emplace_back(node.value);
        }
      }
    }

    std::vector<std::filesystem::path> unique;
    for (const auto &path : paths) {
      if (std::ranges::find(unique, path) == unique.end()) {
        unique.push_back(path);
      }
    }
    return unique;
  }

  std::optional<steam_app_t> parse_app_manifest(std::string_view text) {
    const auto document = parse_vdf(text);
    if (!document) {
      return std::nullopt;
    }

    const auto *state = find(*document, "AppState");
    if (!state) {
      return std::nullopt;
    }

    const auto *app_id = find(state->children, "appid");
    const auto *name = find(state->children, "name");
    if (!app_id || !name || app_id->value.empty() || name->value.empty()) {
      return std::nullopt;
    }

    const auto *flags = find(state->children, "StateFlags");
    if (flags) {
      const int value = std::atoi(flags->value.c_str());
      if ((value & state_fully_installed) == 0) {
        return std::nullopt;
      }
    }

    steam_app_t app {app_id->value, name->value, {}};
    if (const auto *install_dir = find(state->children, "installdir")) {
      app.install_dir = install_dir->value;
    }
    return app;
  }

  std::optional<std::filesystem::path> find_steam_root() {
    std::error_code error;

#ifdef _WIN32
    wchar_t buffer[1024] {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS) {
      const std::filesystem::path path {buffer};
      if (std::filesystem::exists(path / "steamapps", error)) {
        return path;
      }
      error.clear();
    }
#endif

    for (const auto &candidate : {
           std::filesystem::path {"C:\\Program Files (x86)\\Steam"},
           std::filesystem::path {"C:\\Program Files\\Steam"},
         }) {
      if (std::filesystem::exists(candidate / "steamapps", error)) {
        return candidate;
      }
      error.clear();
    }

    return std::nullopt;
  }

  std::vector<steam_app_t> scan_installed(const std::filesystem::path &steam_root) {
    std::vector<steam_app_t> apps;
    if (steam_root.empty()) {
      return apps;
    }

    std::vector<std::filesystem::path> libraries {steam_root};
    const auto library_file = steam_root / "steamapps" / "libraryfolders.vdf";
    if (std::ifstream input {library_file, std::ios::binary}) {
      std::ostringstream contents;
      contents << input.rdbuf();
      if (const auto document = parse_vdf(contents.str())) {
        for (auto &path : library_paths(*document)) {
          if (std::ranges::find(libraries, path) == libraries.end()) {
            libraries.push_back(std::move(path));
          }
        }
      } else {
        BOOST_LOG(warning) << "Couldn't parse ["sv << library_file.string() << ']';
      }
    }

    std::vector<std::string> seen;
    for (const auto &library : libraries) {
      std::error_code error;
      const auto steamapps = library / "steamapps";
      if (!std::filesystem::is_directory(steamapps, error)) {
        continue;
      }

      for (const auto &entry : std::filesystem::directory_iterator {steamapps, error}) {
        const auto filename = entry.path().filename().string();
        if (!filename.starts_with("appmanifest_") || !filename.ends_with(".acf")) {
          continue;
        }

        std::ifstream input {entry.path(), std::ios::binary};
        if (!input) {
          continue;
        }
        std::ostringstream contents;
        contents << input.rdbuf();

        auto app = parse_app_manifest(contents.str());
        if (!app || std::ranges::find(seen, app->app_id) != seen.end()) {
          continue;
        }

        seen.push_back(app->app_id);
        apps.push_back(std::move(*app));
      }
    }

    std::ranges::sort(apps, [](const steam_app_t &left, const steam_app_t &right) {
      return left.name < right.name;
    });
    return apps;
  }

  std::string launch_command(const std::filesystem::path &steam_exe, std::string_view app_id) {
    return "\""s + steam_exe.string() + "\" -applaunch "s + std::string {app_id};
  }
}  // namespace steam_library
