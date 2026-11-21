/**
 * @file src/httpcommon.h
 * @brief Declarations for common HTTP.
 */
#pragma once

// standard includes
#include <string>
#include <vector>

// lib includes
#include <curl/curl.h>

// local includes
#include "network.h"
#include "thread_safe.h"

namespace http {

  /**
   * @brief Initialize shared HTTP client state.
   *
   * @return 0 when HTTP state initializes successfully; nonzero on failure.
   */
  int init();
  /**
   * @brief Generate HTTPS credential files from the provided key and certificate paths.
   *
   * @param pkey Private key PEM data or private key file path.
   * @param cert Certificate data or object used by the operation.
   * @return Created creds object or status.
   */
  int create_creds(const std::string &pkey, const std::string &cert);
  int save_user_creds(
    const std::string &file,
    const std::string &username,
    const std::string &password,
    bool run_our_mouth = false
  );

  /**
   * @brief Reload Web UI user credentials from disk.
   *
   * @param file Destination path for the downloaded content.
   * @return 0 when credentials reload successfully; nonzero on failure.
   */
  int reload_user_creds(const std::string &file);
  /**
   * @brief Download a URL to a local file using libcurl.
   *
   * @param url URL used for the HTTP request.
   * @param file Destination path for the downloaded content.
   * @param ssl_version libcurl TLS version selector for the request.
   * @return True when the file is downloaded successfully.
   */
  bool download_file(const std::string &url, const std::string &file, long ssl_version = CURL_SSLVERSION_TLSv1_2);
  /**
   * @brief Perform an HTTP request and return the response body.
   *
   * @param url URL used for the HTTP request.
   * @param out Receives the response body on success.
   * @param headers Extra request headers, such as an API key.
   * @param post_body Body to send with a POST request; empty for a GET request.
   * @param timeout_seconds Whole-request timeout in seconds.
   * @return True when the request completed with a success status.
   */
  bool fetch_string(
    const std::string &url,
    std::string &out,
    const std::vector<std::string> &headers = {},
    const std::string &post_body = {},
    long timeout_seconds = 15
  );

  /**
   * @brief Percent-encode a string for safe inclusion in a URL.
   *
   * @param url URL used for the HTTP request.
   * @return Percent-encoded URL component.
   */
  std::string url_escape(const std::string &url);
  /**
   * @brief Extract the host component from a URL.
   *
   * @param url URL used for the HTTP request.
   * @return Host name parsed from the URL, or an empty string when none is present.
   */
  std::string url_get_host(const std::string &url);

  extern std::string unique_id;
  extern net::net_e origin_web_ui_allowed;

}  // namespace http
