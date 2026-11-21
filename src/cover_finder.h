/**
 * @file src/cover_finder.h
 * @brief Cover art search for applications through IGDB and the Steam store.
 */
#pragma once

// standard includes
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cover_finder {
  /**
   * @brief A cover candidate returned by a cover source.
   */
  struct candidate_t {
    std::string source;  ///< Source name, either `IGDB` or `Steam`.
    std::string title;  ///< Title reported by the source.
    std::string thumbnail_url;  ///< Small image for the picker.
    std::string image_url;  ///< Full-size cover downloaded when the candidate is chosen.
    std::string key;  ///< Stable key used to name the stored cover file.
  };

  /**
   * @brief Reduce a title to a form that compares well between sources.
   *
   * Case, trademark symbols, punctuation, edition suffixes such as `Definitive Edition`, and trailing roman numerals
   * are removed, so `Hades II` and `Hades 2` normalize to the same value.
   *
   * @param title Title to normalize.
   * @return Normalized title with single spaces between words.
   */
  std::string normalize_title(std::string_view title);

  /**
   * @brief Whether a stored cover image may be served to a client.
   *
   * Only images inside the downloaded cover directory or the bundled assets directory qualify, so a crafted
   * `image-path` cannot read arbitrary files through the cover endpoint.
   *
   * @param path Path to the resolved cover image.
   * @param covers_directory Directory where downloaded covers are stored.
   * @param assets_directory Directory holding the bundled artwork.
   * @return True when the path is inside either directory.
   */
  bool is_allowed_cover_path(std::string_view path, const std::filesystem::path &covers_directory, const std::filesystem::path &assets_directory);

  /**
   * @brief Drop cached cover results and IGDB access tokens.
   */
  void clear_caches();

  /**
   * @brief Whether a host may serve a cover image.
   *
   * @param host Host name parsed from a candidate URL.
   * @return True for the IGDB and Steam image hosts.
   */
  bool is_allowed_cover_host(std::string_view host);

  /**
   * @brief Build the Steam portrait cover URL for an application.
   *
   * @param app_id Steam application ID.
   * @return Full-size cover URL.
   */
  std::string steam_cover_url(int app_id);

  /**
   * @brief Build the Steam thumbnail URL for an application.
   *
   * @param app_id Steam application ID.
   * @return Thumbnail URL.
   */
  std::string steam_thumbnail_url(int app_id);

  /**
   * @brief Convert an IGDB image URL into a full-size PNG cover URL.
   *
   * @param url IGDB image URL, as returned by the API.
   * @return Cover URL, or an empty string when the URL is not an IGDB image.
   */
  std::string igdb_cover_url(std::string_view url);

  /**
   * @brief Parse a Steam store search response.
   *
   * @param json Response body.
   * @return Cover candidates for every application in the response.
   */
  std::vector<candidate_t> parse_steam_results(std::string_view json);

  /**
   * @brief Parse an IGDB games response.
   *
   * @param json Response body.
   * @return Cover candidates for every game that has a cover.
   */
  std::vector<candidate_t> parse_igdb_results(std::string_view json);

  /**
   * @brief Search every configured cover source for a title.
   *
   * IGDB is queried when its credentials are configured; the Steam store is always queried.
   *
   * @param name Title to search for.
   * @return Candidates with exact title matches first.
   */
  std::vector<candidate_t> search(std::string_view name);
}  // namespace cover_finder
