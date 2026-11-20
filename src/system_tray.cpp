/**
 * @file src/system_tray.cpp
 * @brief Definitions for the system tray icon and notification system.
 */
// macros
#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1

  /**
   * @def TRAY_ICON
   * @brief Path to the default system tray icon.
   */
  #define TRAY_ICON WEB_DIR "images/logo-sunshine.svg"
  /**
   * @def TRAY_ICON_PLAYING
   * @brief Path to the system tray icon used while streaming.
   */
  #define TRAY_ICON_PLAYING WEB_DIR "images/sunshine-playing.svg"
  /**
   * @def TRAY_ICON_PAUSING
   * @brief Path to the system tray icon used while streaming is paused.
   */
  #define TRAY_ICON_PAUSING WEB_DIR "images/sunshine-pausing.svg"
  /**
   * @def TRAY_ICON_LOCKED
   * @brief Path to the system tray icon used for pairing requests.
   */
  #define TRAY_ICON_LOCKED WEB_DIR "images/sunshine-locked.svg"
  /**
   * @def WIN32_LEAN_AND_MEAN
   * @brief Macro for WIN32 LEAN AND MEAN.
   */
  #define WIN32_LEAN_AND_MEAN
  #include <AccCtrl.h>
  #include <AclAPI.h>

  // standard includes
  #include <array>
  #include <atomic>
  #include <chrono>
  #include <csignal>
  #include <cstddef>
  #include <filesystem>
  #include <format>
  #include <functional>
  #include <mutex>
  #include <optional>
  #include <string>
  #include <string_view>
  #include <system_error>
  #include <thread>
  #include <unordered_map>
  #include <utility>

  // lib includes
  #include <boost/filesystem.hpp>
  #include <tray.h>

  // local includes
  #include "config.h"
  #include "confighttp.h"
  #include "display_device.h"
  #include "logging.h"
  #include "platform/common.h"
  #include "process.h"
  #include "src/entry_handler.h"
  #include "string_hash.h"
  #include "system_tray.h"
  #include "thread_safe.h"
  #include "platform/windows/utf_utils.h"

using namespace std::literals;

// system_tray namespace
namespace system_tray {
  namespace {
    /**
     * @brief Access the process-wide tray initialization state.
     *
     * @return Atomic initialization flag shared by tray operations.
     */
    std::atomic_bool &tray_initialized_state() {
      static std::atomic_bool initialized {false};
      return initialized;
    }

    /**
     * @brief Access the mutex that serializes tray data and UI changes.
     *
     * @return Mutex shared by tray operations.
     */
    std::mutex &tray_state_mutex() {
      static std::mutex mutex;
      return mutex;
    }

    /**
     * @brief Access the owned tray worker thread.
     *
     * @return Worker thread used by the threaded tray lifecycle.
     */
    std::jthread &tray_worker_thread() {
      static std::jthread worker;
      return worker;
    }

    /**
     * @brief Access storage for the ViGEmBus compatibility notification.
     *
     * @return Persistent string storage backing the tray notification pointer.
     */
    std::string &vigembus_driver_notification_text_storage() {
      static std::string notification_text;
      return notification_text;
    }

  }  // namespace

  void tray_open_ui_cb([[maybe_unused]] struct tray_menu *item) {
    BOOST_LOG(info) << "Opening UI from system tray"sv;
    launch_ui();
  }

  void tray_donate_github_cb([[maybe_unused]] struct tray_menu *item) {
    platf::open_url("https://github.com/sponsors/LizardByte");
  }

  void tray_donate_patreon_cb([[maybe_unused]] struct tray_menu *item) {
    platf::open_url("https://www.patreon.com/LizardByte");
  }

  void tray_donate_paypal_cb([[maybe_unused]] struct tray_menu *item) {
    platf::open_url("https://www.paypal.com/paypalme/ReenigneArcher");
  }

  /**
   * @brief Forwards Qt log messages to Sunshine's BOOST_LOG logger.
   * @param level Log level: 0=debug, 1=info, 2=warning, 3=error.
   * @param msg The message string from Qt.
   */
  static void qt_log_to_boost(int level, const char *msg) {
    if (msg == nullptr) {
      return;
    }
    switch (level) {
      case 0:
        BOOST_LOG(debug) << "Qt: " << msg;
        break;
      case 1:
        BOOST_LOG(info) << "Qt: " << msg;
        break;
      case 2:
        BOOST_LOG(warning) << "Qt: " << msg;
        break;
      default:
        BOOST_LOG(error) << "Qt: " << msg;
        break;
    }
  }

  void tray_reset_display_device_config_cb([[maybe_unused]] struct tray_menu *item) {
    BOOST_LOG(info) << "Resetting display device config from system tray"sv;

    std::ignore = display_device::reset_persistence();
  }

  void tray_restart_cb([[maybe_unused]] struct tray_menu *item) {
    BOOST_LOG(info) << "Restarting from system tray"sv;

    platf::restart();
  }

  void tray_quit_cb([[maybe_unused]] struct tray_menu *item) {
    BOOST_LOG(info) << "Quitting from system tray"sv;

  // If we're running in a service, return a special status to
  // tell it to terminate too, otherwise it will just respawn us.
  if (GetConsoleWindow() == nullptr) {
    lifetime::exit_sunshine(ERROR_SHUTDOWN_IN_PROGRESS, true);
    return;
  }

    lifetime::exit_sunshine(0, true);
  }

  // Tray menu
  static struct tray tray = {
    .icon = TRAY_ICON,
    .tooltip = PROJECT_NAME,
    .menu =
      (struct tray_menu[]) {
        // Tray menu labels currently use the project's English source strings.
        {.text = "Open Sunshine", .cb = tray_open_ui_cb},
        {.text = "-"},
        {.text = "Donate",
         .submenu =
           (struct tray_menu[]) {
             {.text = "GitHub Sponsors", .cb = tray_donate_github_cb},
             {.text = "Patreon", .cb = tray_donate_patreon_cb},
             {.text = "PayPal", .cb = tray_donate_paypal_cb},
             {.text = nullptr}
           }},
        {.text = "-"},
  // Currently display device settings are only supported on Windows
        {.text = "Reset Display Device Config", .cb = tray_reset_display_device_config_cb},
        {.text = "Restart", .cb = tray_restart_cb},
        {.text = "Quit", .cb = tray_quit_cb},
        {.text = nullptr}
      },
  .iconPathCount = 4,
  .allIconPaths = {TRAY_ICON, TRAY_ICON_LOCKED, TRAY_ICON_PLAYING, TRAY_ICON_PAUSING},
  };

  #ifdef SUNSHINE_TESTS
  const struct tray &tray_data_for_testing() {
    return tray;
  }

  bool tray_initialized_for_testing() {
    return tray_initialized_state().load();
  }

  void reset_tray_data_for_testing() {
    const std::scoped_lock lock(tray_state_mutex());
    tray.icon = tray.allIconPaths[0];
    tray.tooltip = PROJECT_NAME;
    tray.notification_icon = nullptr;
    tray.notification_text = nullptr;
    tray.notification_title = nullptr;
    tray.notification_cb = nullptr;
  }
  #endif

  /**
   * @brief Clear notification fields before replacing the tray state.
   */
  void clear_tray_notification() {
    tray.notification_title = nullptr;
    tray.notification_text = nullptr;
    tray.notification_cb = nullptr;
    tray.notification_icon = nullptr;
  }

  /**
   * @brief Show a startup notification when the installed ViGEmBus release is unsupported.
   *
   * @param installed Whether ViGEmBus is installed.
   * @param version Installed ViGEmBus version.
   * @param version_compatible Whether Sunshine supports the installed version.
   * @param supported_versions User-visible supported version range.
   */
  void update_tray_vigembus_driver(
    const bool installed,
    const std::string_view version,
    const bool version_compatible,
    const std::string_view supported_versions
  ) {
    if (!installed || version_compatible) {
      return;
    }

    const std::scoped_lock lock(tray_state_mutex());
    clear_tray_notification();

    const auto displayed_version = version.empty() ? "unknown" : std::format("v{}", version);
    auto &notification_text = vigembus_driver_notification_text_storage();
    notification_text = std::format(
      "Installed ViGEmBus {} is not supported by this version of Sunshine. Supported versions: {}. Restart Sunshine after updating. Click for instructions.",
      displayed_version,
      supported_versions
    );
    tray.notification_title = "Update ViGEmBus";
    tray.notification_text = notification_text.c_str();
    tray.notification_icon = tray.allIconPaths[0];
    tray.notification_cb = []() {
      launch_ui("/troubleshooting#vigembus");
    };

    BOOST_LOG(warning) << notification_text;
    if (tray_initialized_state().load()) {
      tray_update(&tray);
    }
  }

  /**
   * @brief Query the installed ViGEmBus version and prepare its startup notification.
   */
  void prepare_tray_vigembus_driver() {
    const auto status = confighttp::get_vigembus_driver_status();
    update_tray_vigembus_driver(
      status.value("installed", false),
      status.value("version", std::string {}),
      status.value("version_compatible", false),
      status.value("supported_versions", std::string {})
    );
  }

  /**
   * @brief Get resource path.
   *
   * @param relativePath Relative path.
   * @return Absolute path to the resource file for the current platform bundle layout.
   */
  const char *GetResourcePath(const char *relativePath) {
    if (!relativePath || !*relativePath) {
      return nullptr;
    }

    if (std::filesystem::path {relativePath}.is_absolute()) {
      return relativePath;
    }

  // Simple cache ensures our string pointers live forever.
    static std::unordered_map<std::string, std::string, util::transparent_string_hash, std::equal_to<>> g_cache;
    auto search = g_cache.find(std::string_view {relativePath});
    if (search != g_cache.end()) {
      return search->second.c_str();
    }

    std::array<wchar_t, 32768> executable {};
    const auto executable_length = GetModuleFileNameW(nullptr, executable.data(), executable.size());
    if (executable_length == 0 || executable_length >= executable.size()) {
      return relativePath;
    }

    const auto full_path =
      (std::filesystem::path {executable.data()}.parent_path() / utf_utils::from_utf8(relativePath)).lexically_normal();
    auto full = utf_utils::to_utf8(full_path.wstring());

    BOOST_LOG(debug) << "System Tray: using " << full << " for icon path";

    const auto it = g_cache.try_emplace(relativePath, std::move(full)).first;
    return it->second.c_str();
  }

  /**
   * @brief Resolve tray icons against the current executable or application bundle.
   */
  void resolve_tray_icon_paths() {
  std::optional<int> notification_icon_index;
    if (tray.notification_icon != nullptr) {
      for (int index = 0; index < tray.iconPathCount; ++index) {
        if (std::string_view {tray.notification_icon} == tray.allIconPaths[index]) {
          notification_icon_index = index;
          break;
        }
      }
    }

    for (int index = 0; index < tray.iconPathCount; ++index) {
      tray.allIconPaths[index] = GetResourcePath(tray.allIconPaths[index]);
    }
    tray.icon = tray.allIconPaths[0];
  if (notification_icon_index.has_value()) {
    tray.notification_icon = tray.allIconPaths[*notification_icon_index];
  }
  }

  #ifdef SUNSHINE_TESTS
  const char *resource_path_for_testing(const char *relative_path) {
    return GetResourcePath(relative_path);
  }

  void resolve_tray_icon_paths_for_testing() {
    resolve_tray_icon_paths();
  }
  #endif

  /**
   * @brief Initialize the tray, optionally observing an owning thread's stop request.
   *
   * @param stop_token Stop token supplied by the managed tray worker, or an empty token for direct initialization.
   * @return Zero when initialization succeeds; otherwise, a non-zero error code.
   */
  int init_tray_with_stop_token(const std::stop_token stop_token) {
  // If we're running as SYSTEM, Explorer.exe will not have permission to open our thread handle
    // to monitor for thread termination. If Explorer fails to open our thread, our tray icon
    // will persist forever if we terminate unexpectedly. To avoid this, we will modify our thread
    // DACL to add an ACE that allows SYNCHRONIZE access to Everyone.
    {
      PACL old_dacl;
      PSECURITY_DESCRIPTOR sd;
      auto security_error = GetSecurityInfo(GetCurrentThread(), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old_dacl, nullptr, &sd);
      if (security_error != ERROR_SUCCESS) {
        BOOST_LOG(warning) << "GetSecurityInfo() failed: "sv << security_error;
        return 1;
      }

      auto free_sd = util::fail_guard([sd]() {
        LocalFree(sd);
      });

      SID_IDENTIFIER_AUTHORITY sid_authority = SECURITY_WORLD_SID_AUTHORITY;
      PSID world_sid;
      if (!AllocateAndInitializeSid(&sid_authority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &world_sid)) {
        security_error = GetLastError();
        BOOST_LOG(warning) << "AllocateAndInitializeSid() failed: "sv << security_error;
        return 1;
      }

      auto free_sid = util::fail_guard([world_sid]() {
        FreeSid(world_sid);
      });

      EXPLICIT_ACCESS ea {};
      ea.grfAccessPermissions = SYNCHRONIZE;
      ea.grfAccessMode = GRANT_ACCESS;
      ea.grfInheritance = NO_INHERITANCE;
      ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
      ea.Trustee.ptstrName = (LPSTR) world_sid;

      PACL new_dacl;
      security_error = SetEntriesInAcl(1, &ea, old_dacl, &new_dacl);
      if (security_error != ERROR_SUCCESS) {
        BOOST_LOG(warning) << "SetEntriesInAcl() failed: "sv << security_error;
        return 1;
      }

      auto free_new_dacl = util::fail_guard([new_dacl]() {
        LocalFree(new_dacl);
      });

      security_error = SetSecurityInfo(GetCurrentThread(), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, new_dacl, nullptr);
      if (security_error != ERROR_SUCCESS) {
        BOOST_LOG(warning) << "SetSecurityInfo() failed: "sv << security_error;
        return 1;
      }
    }

    // Wait for the shell to be initialized before registering the tray icon.
    // This ensures the tray icon works reliably after a logoff/logon cycle.
    while (GetShellWindow() == nullptr && !stop_token.stop_requested()) {
      Sleep(1000);
    }
  if (stop_token.stop_requested()) {
    return 1;
  }

  resolve_tray_icon_paths();

    tray_set_log_callback(qt_log_to_boost);

    tray_set_app_info(PROJECT_NAME, PROJECT_NAME, PROJECT_FQDN);

    {
      const std::scoped_lock lock(tray_state_mutex());
      if (stop_token.stop_requested()) {
        return 1;
      }
      if (tray_init(&tray) < 0) {
        BOOST_LOG(warning) << "Failed to create system tray"sv;
        return 1;
      }

      tray_initialized_state().store(true);
    }

    BOOST_LOG(info) << "System tray created"sv;
    return 0;
  }

  int init_tray() {
    return init_tray_with_stop_token({});
  }

  int process_tray_events() {
    if (!tray_initialized_state().load()) {
      BOOST_LOG(error) << "System tray is not initialized"sv;
      return 1;
    }

    // Block until an event is processed or tray_quit() is called
    return tray_loop(1);
  }

  void run_tray_until_exit(const std::shared_ptr<safe::event_t<bool>> &shutdown_event) {
    if (!tray_initialized_state().load()) {
      BOOST_LOG(info) << "System tray is unavailable; continuing without it"sv;
      shutdown_event->view();
      return;
    }

    while (process_tray_events() == 0);
    shutdown_event->raise(true);
  }

  int end_tray() {
    auto &worker = tray_worker_thread();
    worker.request_stop();

    {
      const std::scoped_lock lock(tray_state_mutex());
      if (tray_initialized_state().exchange(false)) {
        tray_exit();
      }
    }

    if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) {
      worker.join();
    }
    return 0;
  }

  void update_tray_playing(std::string app_name) {
    const std::scoped_lock lock(tray_state_mutex());
    if (!tray_initialized_state().load()) {
      return;
    }

    tray.notification_title = nullptr;
    tray.notification_text = nullptr;
    tray.notification_cb = nullptr;
    tray.notification_icon = nullptr;
    tray.icon = tray.allIconPaths[2];
    tray_update(&tray);
    tray.icon = tray.allIconPaths[2];
    tray.notification_title = "Stream Started";

    static std::string msg = std::format("Streaming started for {}", app_name);
    tray.notification_text = msg.c_str();
    tray.tooltip = msg.c_str();
    tray.notification_icon = tray.allIconPaths[2];
    tray_update(&tray);
  }

  void update_tray_pausing(std::string app_name) {
    const std::scoped_lock lock(tray_state_mutex());
    if (!tray_initialized_state().load()) {
      return;
    }

    tray.notification_title = nullptr;
    tray.notification_text = nullptr;
    tray.notification_cb = nullptr;
    tray.notification_icon = nullptr;
    tray.icon = tray.allIconPaths[3];
    tray_update(&tray);

    static std::string msg = std::format("Streaming paused for {}", app_name);
    tray.icon = tray.allIconPaths[3];
    tray.notification_title = "Stream Paused";
    tray.notification_text = msg.c_str();
    tray.tooltip = msg.c_str();
    tray.notification_icon = tray.allIconPaths[3];
    tray_update(&tray);
  }

  void update_tray_stopped(std::string app_name) {
    const std::scoped_lock lock(tray_state_mutex());
    if (!tray_initialized_state().load()) {
      return;
    }

    tray.notification_title = nullptr;
    tray.notification_text = nullptr;
    tray.notification_cb = nullptr;
    tray.notification_icon = nullptr;
    tray.icon = tray.allIconPaths[0];
    tray_update(&tray);

    static std::string msg = std::format("Application {} successfully stopped", app_name);
    tray.icon = tray.allIconPaths[0];
    tray.notification_icon = tray.allIconPaths[0];
    tray.notification_title = "Application Stopped";
    tray.notification_text = msg.c_str();
    tray.tooltip = PROJECT_NAME;
    tray_update(&tray);
  }

  void update_tray_require_pin() {
    const std::scoped_lock lock(tray_state_mutex());
    if (!tray_initialized_state().load()) {
      return;
    }

    tray.notification_title = nullptr;
    tray.notification_text = nullptr;
    tray.notification_cb = nullptr;
    tray.notification_icon = nullptr;
    tray.icon = tray.allIconPaths[0];
    tray_update(&tray);
    tray.icon = tray.allIconPaths[0];
    tray.notification_title = "Incoming Pairing Request";
    tray.notification_text = "Click here to complete the pairing process";
    tray.notification_icon = tray.allIconPaths[1];
    tray.tooltip = PROJECT_NAME;
    tray.notification_cb = []() {
      launch_ui("/pin");
    };
    tray_update(&tray);
  }

  // Threading functions available on all platforms
  /**
   * @brief Run the managed system tray event loop.
   *
   * @param stop_token Stop token owned by the tray worker thread.
   */
  static void tray_thread_worker(const std::stop_token stop_token) {
    platf::set_thread_name("system_tray");
    BOOST_LOG(info) << "System tray thread started"sv;

    // Initialize the tray in this thread
    if (init_tray_with_stop_token(stop_token) != 0) {
      if (!stop_token.stop_requested()) {
        BOOST_LOG(error) << "Failed to initialize tray in thread"sv;
      }
      return;
    }

    // Main tray event loop
    while (!stop_token.stop_requested() && process_tray_events() == 0) {
      // Continue until the tray requests shutdown or event processing stops.
    }

    {
      const std::scoped_lock lock(tray_state_mutex());
      tray_initialized_state().store(false);
    }

    BOOST_LOG(info) << "System tray thread ended"sv;
  }

  int init_tray_threaded() {
    try {
      auto &worker = tray_worker_thread();
      if (worker.joinable()) {
        BOOST_LOG(error) << "System tray thread is already running"sv;
        return 1;
      }
      worker = std::jthread(tray_thread_worker);

      BOOST_LOG(info) << "System tray thread initialized successfully"sv;
      return 0;
    } catch (const std::system_error &e) {
      BOOST_LOG(error) << "Failed to create tray thread: " << e.what();
      return 1;
    }
  }

}  // namespace system_tray
#endif
