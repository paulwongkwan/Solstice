/**
 * @file src/system_tray.h
 * @brief Declarations for the system tray icon and notification system.
 */
#pragma once

// standard includes
#include <memory>
#include <string>
#include <string_view>

namespace safe {
  template<class T>
  class event_t;
}

/**
 * @brief Handles the system tray icon and notification system.
 */
namespace system_tray {
  /**
   * @brief Callback for opening the UI from the system tray.
   * @param item The tray menu item.
   */
  void tray_open_ui_cb([[maybe_unused]] struct tray_menu *item);

  /**
   * @brief Callback for resetting display device configuration.
   * @param item The tray menu item.
   */
  void tray_reset_display_device_config_cb([[maybe_unused]] struct tray_menu *item);

  /**
   * @brief Callback for restarting Solstice from the system tray.
   * @param item The tray menu item.
   */
  void tray_restart_cb([[maybe_unused]] struct tray_menu *item);

  /**
   * @brief Callback for exiting Solstice from the system tray.
   * @param item The tray menu item.
   */
  void tray_quit_cb([[maybe_unused]] struct tray_menu *item);

  /**
   * @brief Initializes the system tray without starting a loop.
   * @return 0 if initialization was successful, non-zero otherwise.
   */
  int init_tray();

  /**
   * @brief Processes a single tray event iteration.
   * @return 0 if processing was successful, non-zero otherwise.
   */
  int process_tray_events();

  /**
   * @brief Process tray events until exit and notify the application's workers.
   *
   * If tray initialization failed, wait for the application's shutdown event instead
   * of stopping its workers. An initialized tray still requests shutdown when its loop exits.
   *
   * @param shutdown_event Event used to stop Solstice's server threads.
   */
  void run_tray_until_exit(const std::shared_ptr<safe::event_t<bool>> &shutdown_event);

  /**
   * @brief Exit the system tray.
   * @return 0 after exiting the system tray.
   */
  int end_tray();

  /**
   * @brief Sets the tray icon in playing mode and spawns the appropriate notification
   * @param app_name The started application name
   */
  void update_tray_playing(std::string app_name);

  /**
   * @brief Sets the tray icon in pausing mode (stream stopped but app running) and spawns the appropriate notification
   * @param app_name The paused application name
   */
  void update_tray_pausing(std::string app_name);

  /**
   * @brief Sets the tray icon in stopped mode (app and stream stopped) and spawns the appropriate notification
   * @param app_name The started application name
   */
  void update_tray_stopped(std::string app_name);

  /**
   * @brief Spawns a notification for PIN Pairing. Clicking it opens the PIN Web UI Page
   */
  void update_tray_require_pin();

  /**
   * @brief Show an update notification when the installed ViGEmBus release is unsupported.
   *
   * Existing notifications are preserved when ViGEmBus is absent or its version is supported.
   *
   * @param installed Whether ViGEmBus is installed.
   * @param version Installed ViGEmBus version.
   * @param version_compatible Whether Solstice supports the installed version.
   * @param supported_versions User-visible supported version range.
   */
  void update_tray_vigembus_driver(
    bool installed,
    std::string_view version,
    bool version_compatible,
    std::string_view supported_versions
  );

  /**
   * @brief Query the ViGEmBus version and prepare its startup notification.
   */
  void prepare_tray_vigembus_driver();

  /**
   * @brief Initializes and runs the system tray in a separate thread.
   * @return 0 if initialization was successful, non-zero otherwise.
   */
  int init_tray_threaded();

#ifdef SOLSTICE_TESTS
  /**
   * @brief Get the tray data used by the system tray implementation.
   *
   * @return Read-only tray data for unit-test assertions.
   */
  const struct tray &tray_data_for_testing();

  /**
   * @brief Check whether the system tray is initialized.
   *
   * @return true if the system tray is initialized; otherwise, false.
   */
  bool tray_initialized_for_testing();

  /**
   * @brief Restore the persistent tray data to its initial state between tests.
   */
  void reset_tray_data_for_testing();

  /**
   * @brief Resolve a tray resource path using the production platform logic.
   *
   * @param relative_path Resource path relative to the executable or application bundle.
   * @return Stable resource path used by the tray backend.
   */
  const char *resource_path_for_testing(const char *relative_path);

  /**
   * @brief Resolve all tray icon paths using the production platform logic.
   */
  void resolve_tray_icon_paths_for_testing();
#endif
}  // namespace system_tray
