/**
 * @file src/main.cpp
 * @brief Definitions for the main entry point for Sunshine.
 */
// standard includes
#include <array>
#include <cerrno>
#include <codecvt>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

// lib includes
#include <rs.h>

// platform includes
/**
 * @def WIN32_LEAN_AND_MEAN
 * @brief Excludes the Winsock 1 headers so Boost.Asio can include Winsock 2.
 */
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// local includes
#include "confighttp.h"
#include "display_device.h"
#include "entry_handler.h"
#include "globals.h"
#include "httpcommon.h"
#include "logging.h"
#include "main.h"
#include "nvhttp.h"
#include "platform/permissions.h"
#include "process.h"
#include "system_tray.h"
#include "upnp.h"
#include "video.h"

using namespace std::literals;

std::map<int, std::function<void()>> signal_handlers;  ///< Signal handlers.

/**
 * @brief Read the Windows build number of the running operating system.
 *
 * @return Build number, or zero when it cannot be determined.
 */
DWORD windows_build_number() {
  std::array<wchar_t, 32> buffer {};
  DWORD size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
  if (RegGetValueW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        L"CurrentBuildNumber",
        RRF_RT_REG_SZ,
        nullptr,
        buffer.data(),
        &size
      ) != ERROR_SUCCESS) {
    return 0;
  }

  return static_cast<DWORD>(std::wcstoul(buffer.data(), nullptr, 10));
}

/**
 * @brief Forward a POSIX signal to the registered Sunshine handler.
 *
 * @param sig Native signal number being handled.
 */
void on_signal_forwarder(int sig) {
  signal_handlers.at(sig)();
}

/**
 * @brief Register the handler invoked for a POSIX signal.
 *
 * @param sig Native signal number being handled.
 * @param fn Signal handler function to install.
 */
template<class FN>
void on_signal(int sig, FN &&fn) {
  signal_handlers.emplace(sig, std::forward<FN>(fn));

  std::signal(sig, on_signal_forwarder);
}

/**
 * @brief Dispatch commands through a common signature, ignoring arguments each command does not need.
 */
std::map<std::string_view, std::function<int(const char *name, int argc, char **argv)>> cmd_to_func {
  {"creds"sv, [](const char *name, int argc, char **argv) {
     return args::creds(name, argc, argv);
   }},
  {"help"sv, [](const char *name, int, char **) {
     return args::help(name);
   }},
  {"version"sv, [](const char *, int, char **) {
     return args::version();
   }},
  {"restore-nvprefs-undo"sv, [](const char *, int, char **) {
     return args::restore_nvprefs_undo();
   }},
};

/**
 * @brief Handle Windows session-change messages for the monitor window.
 *
 * @param hwnd Window handle receiving the Windows control event.
 * @param uMsg U msg.
 * @param wParam W param.
 * @param lParam L param.
 * @return Process or platform callback exit code.
 */
LRESULT CALLBACK SessionMonitorWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_ENDSESSION:
      {
        // Terminate ourselves with a blocking exit call
        std::cout << "Received WM_ENDSESSION"sv << std::endl;
        lifetime::exit_sunshine(0, false);
        return 0;
      }
    default:
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
}

/**
 * @brief Handle Windows console control events for shutdown.
 *
 * @param type Protocol, message, or resource type selector.
 * @return Process or platform callback exit code.
 */
WINAPI BOOL ConsoleCtrlHandler(DWORD type) {
  if (type == CTRL_CLOSE_EVENT) {
    BOOST_LOG(info) << "Console closed handler called";
    lifetime::exit_sunshine(0, false);
  }
  return FALSE;
}

#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
constexpr bool tray_is_enabled = true;  ///< Compile-time flag indicating tray support is enabled.
#else
constexpr bool tray_is_enabled = false;
#endif

/**
 * @brief Run the main event loop until Sunshine is asked to exit.
 *
 * @param shutdown_event Shutdown event.
 */
void mainThreadLoop(const std::shared_ptr<safe::event_t<bool>> &shutdown_event) {
  // The Windows tray runs in its own thread, so there is no main-thread event loop to run.
  BOOST_LOG(info) << "No main thread features enabled, skipping event loop"sv;
  shutdown_event->view();
}

/**
 * @brief Run the main application or worker loop.
 *
 * @param argc The number of arguments.
 * @param argv The arguments.
 * @return Process or platform callback exit code.
 */
int main(int argc, char *argv[]) {
  lifetime::argv = argv;

  task_pool_util::TaskPool::task_id_t force_shutdown = nullptr;

  // Avoid searching the PATH in case a user has configured their system insecurely
  // by placing a user-writable directory in the system-wide PATH variable.
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);

  setlocale(LC_ALL, "C");

  // Sunshine requires Windows 11 (build 22000) or newer.
  if (const auto build_number = windows_build_number(); build_number != 0 && build_number < 22000) {
    BOOST_LOG(fatal) << "Sunshine requires Windows 11 or newer. Detected Windows build "sv << build_number << '.';
    return 1;
  }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  // Use UTF-8 conversion for the default C++ locale (used by boost::log)
  std::locale::global(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));
#pragma GCC diagnostic pop

  mail::man = std::make_shared<safe::mail_raw_t>();

  // parse config file
  if (config::parse(argc, argv)) {
    return 0;
  }

  auto log_deinit_guard = logging::init(config::sunshine.min_log_level, config::sunshine.log_file);
  if (!log_deinit_guard) {
    BOOST_LOG(error) << "Logging failed to initialize"sv;
  }

  // logging can begin at this point
  // if anything is logged prior to this point, it will appear in stdout, but not in the log viewer in the UI
  // the version should be printed to the log before anything else
  BOOST_LOG(info) << PROJECT_NAME << " version: " << PROJECT_VERSION << " commit: " << PROJECT_VERSION_COMMIT;

  // Log publisher metadata
  log_publisher_data();

  // Log modified_config_settings
  config::log_config_settings(config::modified_config_settings, false);
  config::modified_config_settings.clear();

  if (!config::sunshine.cmd.name.empty()) {
    auto fn = cmd_to_func.find(config::sunshine.cmd.name);
    if (fn == std::end(cmd_to_func)) {
      BOOST_LOG(fatal) << "Unknown command: "sv << config::sunshine.cmd.name;

      BOOST_LOG(info) << "Possible commands:"sv;
      for (auto &[key, _] : cmd_to_func) {
        BOOST_LOG(info) << '\t' << key;
      }

      return 7;
    }

    return fn->second(argv[0], config::sunshine.cmd.argc, config::sunshine.cmd.argv);
  }

  // Adding guard here first as it also performs recovery after crash,
  // otherwise people could theoretically end up without display output.
  // It also should be destroyed before forced shutdown to expedite the cleanup.
  auto display_device_deinit_guard = display_device::init(platf::appdata() / "display_device.state", config::video);
  if (!display_device_deinit_guard) {
    BOOST_LOG(error) << "Display device session failed to initialize"sv;
  }

  // Modify relevant NVIDIA control panel settings if the system has corresponding gpu
  if (nvprefs_instance.load()) {
    // Restore global settings to the undo file left by improper termination of sunshine.exe
    nvprefs_instance.restore_from_and_delete_undo_file_if_exists();
    // Modify application settings for sunshine.exe
    nvprefs_instance.modify_application_profile();
    // Modify global settings, undo file is produced in the process to restore after improper termination
    nvprefs_instance.modify_global_profile();
    // Unload dynamic library to survive driver re-installation
    nvprefs_instance.unload();
  }

  // Wait as long as possible to terminate Sunshine.exe during logoff/shutdown
  SetProcessShutdownParameters(0x100, SHUTDOWN_NORETRY);

  // We must create a hidden window to receive shutdown notifications since we load gdi32.dll
  std::promise<HWND> session_monitor_hwnd_promise;
  auto session_monitor_hwnd_future = session_monitor_hwnd_promise.get_future();
  std::promise<void> session_monitor_join_thread_promise;
  auto session_monitor_join_thread_future = session_monitor_join_thread_promise.get_future();

  std::jthread session_monitor_thread([&]() {
    platf::set_thread_name("session_monitor");
    session_monitor_join_thread_promise.set_value_at_thread_exit();

    WNDCLASSA wnd_class {};
    wnd_class.lpszClassName = "SunshineSessionMonitorClass";
    wnd_class.lpfnWndProc = SessionMonitorWindowProc;
    if (!RegisterClassA(&wnd_class)) {
      session_monitor_hwnd_promise.set_value(nullptr);
      BOOST_LOG(error) << "Failed to register session monitor window class"sv << std::endl;
      return;
    }

    auto wnd = CreateWindowExA(
      0,
      wnd_class.lpszClassName,
      "Sunshine Session Monitor Window",
      0,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      nullptr,
      nullptr,
      nullptr,
      nullptr
    );

    session_monitor_hwnd_promise.set_value(wnd);

    if (!wnd) {
      BOOST_LOG(error) << "Failed to create session monitor window"sv << std::endl;
      return;
    }

    ShowWindow(wnd, SW_HIDE);

    // Run the message loop for our window
    MSG msg {};
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  });

  auto session_monitor_join_thread_guard = util::fail_guard([&]() {
    if (session_monitor_hwnd_future.wait_for(1s) == std::future_status::ready) {
      if (HWND session_monitor_hwnd = session_monitor_hwnd_future.get()) {
        PostMessage(session_monitor_hwnd, WM_CLOSE, 0, 0);
      }

      if (session_monitor_join_thread_future.wait_for(1s) == std::future_status::ready) {
        session_monitor_thread.join();
        return;
      } else {
        BOOST_LOG(warning) << "session_monitor_join_thread_future reached timeout";
      }
    } else {
      BOOST_LOG(warning) << "session_monitor_hwnd_future reached timeout";
    }

    session_monitor_thread.detach();
  });

  task_pool.start(1);

  // Create signal handler after logging has been initialized
  auto shutdown_event = mail::man->event<bool>(mail::shutdown);
  on_signal(SIGINT, [&force_shutdown, &display_device_deinit_guard, shutdown_event]() {
    BOOST_LOG(info) << "Interrupt handler called"sv;

    auto task = []() {
      BOOST_LOG(fatal) << "10 seconds passed, yet Sunshine's still running: Forcing shutdown"sv;
      logging::log_flush();
      lifetime::debug_trap();
    };
    force_shutdown = task_pool.pushDelayed(task, 10s).task_id;

    // Break out of the main loop
    shutdown_event->raise(true);

    if (tray_is_enabled && config::sunshine.system_tray) {
      system_tray::end_tray();
    }

    display_device_deinit_guard = nullptr;
  });

  on_signal(SIGTERM, [&force_shutdown, &display_device_deinit_guard, shutdown_event]() {
    BOOST_LOG(info) << "Terminate handler called"sv;

    auto task = []() {
      BOOST_LOG(fatal) << "10 seconds passed, yet Sunshine's still running: Forcing shutdown"sv;
      logging::log_flush();
      lifetime::debug_trap();
    };
    force_shutdown = task_pool.pushDelayed(task, 10s).task_id;

    // Break out of the main loop
    shutdown_event->raise(true);

    if (tray_is_enabled && config::sunshine.system_tray) {
      system_tray::end_tray();
    }

    display_device_deinit_guard = nullptr;
  });

  // Terminate gracefully on Windows when console window is closed
  SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

  proc::refresh(config::stream.file_apps);

  // If any of the following fail, we log an error and continue event though sunshine will not function correctly.
  // This allows access to the UI to fix configuration problems or view the logs.

  auto platf_deinit_guard = platf::init();
  if (!platf_deinit_guard) {
    BOOST_LOG(error) << "Platform failed to initialize"sv;
  }

  // Capture the pre-request state so access granted during this launch causes
  // one clean restart after every verifiable required permission is available.
  const bool permission_restart_needed = !platf::required_permissions_granted(platf::get_permission_statuses());

  auto proc_deinit_guard = proc::init();
  if (!proc_deinit_guard) {
    BOOST_LOG(error) << "Proc failed to initialize"sv;
  }

  reed_solomon_init();
  auto input_deinit_guard = input::init();

  if (config::input.controller && input::probe_gamepads()) {
    BOOST_LOG(warning) << "No gamepad input is available"sv;
  }

  if (video::probe_encoders()) {
    BOOST_LOG(error) << "Video failed to find working encoder"sv;
  }

  if (http::init()) {
    BOOST_LOG(fatal) << "HTTP interface failed to initialize"sv;

    BOOST_LOG(fatal) << "To relaunch Sunshine successfully, use the shortcut in the Start Menu. Do not run Sunshine.exe manually."sv;
    std::this_thread::sleep_for(10s);

    return -1;
  }

  std::unique_ptr<platf::deinit_t> mDNS;
  auto sync_mDNS = std::async(std::launch::async, [&mDNS]() {
    mDNS = platf::publish::start();
  });

  std::unique_ptr<platf::deinit_t> upnp_unmap;
  auto sync_upnp = std::async(std::launch::async, [&upnp_unmap]() {
    upnp_unmap = upnp::start();
  });

  // FIXME: Temporary workaround: Simple-Web_server needs to be updated or replaced
  if (shutdown_event->peek()) {
    return lifetime::desired_exit_code;
  }

  std::jthread httpThread {nvhttp::start};
  std::jthread configThread {confighttp::start};
  std::jthread rtspThread {rtsp_stream::start};

  // If we're using the default port and GameStream is enabled, warn the user
  if (config::sunshine.port == 47989 && is_gamestream_enabled()) {
    BOOST_LOG(fatal) << "GameStream is still enabled in GeForce Experience! This *will* cause streaming problems with Sunshine!"sv;
    BOOST_LOG(fatal) << "Disable GameStream on the SHIELD tab in GeForce Experience or change the Port setting on the Advanced tab in the Sunshine Web UI."sv;
  }

  if (tray_is_enabled && config::sunshine.system_tray) {
    BOOST_LOG(info) << "Starting system tray"sv;
    system_tray::prepare_tray_vigembus_driver();
    // TODO: Windows has a weird bug where when running as a service and on the first Windows boot,
    // the tray icon would not appear even though Sunshine is running correctly otherwise.
    // Restarting the service would allow the icon to appear normally.
    // For now we will keep the Windows tray icon on a separate thread.
    system_tray::init_tray_threaded();
  }

  std::jthread permission_watcher;
  if (permission_restart_needed) {
    permission_watcher = std::jthread([](std::stop_token stop) {
      while (!stop.stop_requested()) {
        if (platf::required_permissions_granted(platf::get_permission_statuses())) {
          BOOST_LOG(info) << "Required permissions granted; restarting Sunshine"sv;
          platf::restart();
          return;
        }
        std::this_thread::sleep_for(2s);
      }
    });
  }

  mainThreadLoop(shutdown_event);

  permission_watcher.request_stop();
  if (permission_watcher.joinable()) {
    permission_watcher.join();
  }

  httpThread.join();
  configThread.join();
  rtspThread.join();

  task_pool.stop();
  task_pool.join();

  // Restore global NVIDIA control panel settings
  if (nvprefs_instance.owning_undo_file() && nvprefs_instance.load()) {
    nvprefs_instance.restore_global_profile();
    nvprefs_instance.unload();
  }

  return lifetime::desired_exit_code;
}
