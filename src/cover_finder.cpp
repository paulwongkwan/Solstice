/**
 * @file src/cover_finder.cpp
 * @brief Cover art search for applications through IGDB and the Steam store.
 */

// local includes
#include "cover_finder.h"
#include "config.h"
#include "httpcommon.h"
#include "logging.h"

// standard includes
#include <algorithm>
#include <array>
#include <cctype>

// lib includes
#include <nlohmann/json.hpp>

namespace cover_finder {
  using namespace std::literals;

  namespace {
    /**
     * @brief Hosts that are allowed to serve application covers.
     */
    constexpr std::array<std::string_view, 4> allowed_cover_hosts {
      "images.igdb.com",
      "cdn.cloudflare.steamstatic.com",
      "cdn.akamai.steamstatic.com",
      "shared.akamai.steamstatic.com",
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
     * @brief Rank a candidate title against the query.
     *
     * @param title Candidate title.
     * @param query Lowercased query.
     * @return 0 for an exact match, 1 for a prefix, 2 for a substring, and 3 otherwise.
     */
    int title_rank(std::string_view title, std::string_view query) {
      const auto lower = lowercase(title);
      if (lower == query) {
        return 0;
      }
      if (lower.starts_with(query)) {
        return 1;
      }
      if (lower.find(query) != std::string::npos) {
        return 2;
      }
      return 3;
    }

    /**
     * @brief Sort candidates so the closest title matches come first.
     *
     * @param candidates Candidates to order in place.
     * @param query Lowercased query.
     */
    void sort_by_relevance(std::vector<candidate_t> &candidates, std::string_view query) {
      std::stable_sort(candidates.begin(), candidates.end(), [query](const candidate_t &left, const candidate_t &right) {
        return title_rank(left.title, query) < title_rank(right.title, query);
      });
    }

    /**
     * @brief Request a Twitch application access token for IGDB.
     *
     * @param client_id Twitch client ID.
     * @param client_secret Twitch client secret.
     * @return Access token, or an empty string when the request fails.
     */
    std::string igdb_access_token(const std::string &client_id, const std::string &client_secret) {
      std::string body;
      const std::string form = "client_id="s + http::url_escape(client_id) +
                               "&client_secret="s + http::url_escape(client_secret) +
                               "&grant_type=client_credentials"s;
      if (!http::fetch_string("https://id.twitch.tv/oauth2/token"s, body, {"Accept: application/json"s}, form)) {
        BOOST_LOG(warning) << "Couldn't request an IGDB access token"sv;
        return {};
      }

      try {
        const auto tree = nlohmann::json::parse(body);
        return tree.value("access_token", ""s);
      } catch (const std::exception &e) {
        BOOST_LOG(warning) << "Couldn't parse the IGDB access token response: "sv << e.what();
        return {};
      }
    }

    /**
     * @brief Query IGDB for games matching a title.
     *
     * @param name Title to search for.
     * @return Cover candidates; empty when IGDB is not configured or the request fails.
     */
    std::vector<candidate_t> search_igdb(std::string_view name) {
      const auto &client_id = config::solstice.igdb_client_id;
      const auto &client_secret = config::solstice.igdb_client_secret;
      if (client_id.empty() || client_secret.empty()) {
        return {};
      }

      const auto token = igdb_access_token(client_id, client_secret);
      if (token.empty()) {
        return {};
      }

      std::string body;
      const std::string query = "search \""s + std::string {name} + "\"; fields name,cover.url; limit 12;"s;
      if (!http::fetch_string(
            "https://api.igdb.com/v4/games"s,
            body,
            {"Client-ID: "s + client_id, "Authorization: Bearer "s + token, "Accept: application/json"s},
            query)) {
        BOOST_LOG(warning) << "IGDB cover search failed"sv;
        return {};
      }

      return parse_igdb_results(body);
    }
  }  // namespace

  bool is_allowed_cover_host(std::string_view host) {
    const auto lower = lowercase(host);
    return std::ranges::any_of(allowed_cover_hosts, [&lower](std::string_view allowed) {
      return lower == allowed;
    });
  }

  std::string steam_cover_url(int app_id) {
    return "https://cdn.cloudflare.steamstatic.com/steam/apps/"s + std::to_string(app_id) + "/library_600x900_2x.jpg"s;
  }

  std::string steam_thumbnail_url(int app_id) {
    return "https://cdn.cloudflare.steamstatic.com/steam/apps/"s + std::to_string(app_id) + "/library_600x900.jpg"s;
  }

  std::string igdb_cover_url(std::string_view url) {
    if (url.empty()) {
      return {};
    }

    std::string result {url};
    if (result.starts_with("//")) {
      result = "https:"s + result;
    }

    const auto upload = result.find("/upload/");
    if (upload == std::string::npos || !result.starts_with("https://images.igdb.com/")) {
      return {};
    }

    // Normalize to the largest cover size and PNG encoding.
    const auto size_start = upload + "/upload/"s.size();
    const auto size_end = result.find('/', size_start);
    if (size_end == std::string::npos) {
      return {};
    }
    result.replace(size_start, size_end - size_start, "t_cover_big_2x"s);

    if (const auto extension = result.find_last_of('.'); extension != std::string::npos) {
      result.replace(extension, std::string::npos, ".png"s);
    }
    return result;
  }

  std::vector<candidate_t> parse_steam_results(std::string_view json) {
    std::vector<candidate_t> candidates;
    if (json.empty()) {
      return candidates;
    }

    try {
      const auto tree = nlohmann::json::parse(json);
      const auto items = tree.find("items");
      if (items == tree.end() || !items->is_array()) {
        return candidates;
      }

      for (const auto &item : *items) {
        if (item.value("type", ""s) != "app"s) {
          continue;
        }

        const auto app_id = item.value("id", 0);
        const auto title = item.value("name", ""s);
        if (app_id <= 0 || title.empty()) {
          continue;
        }

        candidates.push_back({
          "Steam"s,
          title,
          steam_thumbnail_url(app_id),
          steam_cover_url(app_id),
          "steam_"s + std::to_string(app_id),
        });
      }
    } catch (const std::exception &e) {
      BOOST_LOG(debug) << "Couldn't parse the Steam cover search response: "sv << e.what();
    }

    return candidates;
  }

  std::vector<candidate_t> parse_igdb_results(std::string_view json) {
    std::vector<candidate_t> candidates;
    if (json.empty()) {
      return candidates;
    }

    try {
      const auto tree = nlohmann::json::parse(json);
      if (!tree.is_array()) {
        return candidates;
      }

      for (const auto &game : tree) {
        const auto title = game.value("name", ""s);
        const auto id = game.value("id", 0);
        const auto cover = game.find("cover");
        if (title.empty() || id <= 0 || cover == game.end() || !cover->is_object()) {
          continue;
        }

        const auto cover_url = igdb_cover_url(cover->value("url", ""s));
        if (cover_url.empty()) {
          continue;
        }

        candidates.push_back({
          "IGDB"s,
          title,
          cover_url,
          cover_url,
          "igdb_"s + std::to_string(id),
        });
      }
    } catch (const std::exception &e) {
      BOOST_LOG(debug) << "Couldn't parse the IGDB cover search response: "sv << e.what();
    }

    return candidates;
  }

  std::vector<candidate_t> search(std::string_view name) {
    std::vector<candidate_t> candidates;
    if (name.empty()) {
      return candidates;
    }

    std::string body;
    const auto url = "https://store.steampowered.com/api/storesearch/?term="s + http::url_escape(std::string {name}) +
                     "&l=english&cc=us"s;
    if (http::fetch_string(url, body, {"Accept: application/json"s})) {
      auto steam = parse_steam_results(body);
      candidates.insert(candidates.end(), steam.begin(), steam.end());
    } else {
      BOOST_LOG(warning) << "Steam cover search failed"sv;
    }

    auto igdb = search_igdb(name);
    candidates.insert(candidates.end(), igdb.begin(), igdb.end());

    sort_by_relevance(candidates, lowercase(name));
    return candidates;
  }
}  // namespace cover_finder
