/**
 * @file src/platform/windows/input.cpp
 * @brief Definitions for input handling on Windows.
 */
#ifndef DOXYGEN
  #define WINVER 0x0A00
#endif
#ifdef DOXYGEN
  /**
   * @def CALLBACK
   * @brief Windows callback calling convention marker.
   */
  #define CALLBACK
#endif

// platform includes
#include <array>
#include <Windows.h>

// standard includes
#include <algorithm>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

// lib includes
#include <ViGEm/Client.h>

// local includes
#include "input.h"
#include "misc.h"
#include "src/config.h"
#include "src/globals.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "utf_utils.h"

namespace platf {
  using namespace std::literals;

  /**
   * @brief ViGEm client pointer released with `vigem_free`.
   */
  using client_t = util::safe_ptr<_VIGEM_CLIENT_T, &vigem_free>;
  /**
   * @brief ViGEm target pointer released with `vigem_target_free`.
   */
  using target_t = util::safe_ptr<_VIGEM_TARGET_T, &vigem_target_free>;

  /**
   * @brief Handle Xbox 360 virtual gamepad notification events.
   *
   * @param client ViGEm client.
   * @param target ViGEm target.
   * @param largeMotor Large motor strength.
   * @param smallMotor Small motor strength.
   * @param userdata User data pointer.
   */
  void CALLBACK x360_notify(
    client_t::pointer client,
    target_t::pointer target,
    std::uint8_t largeMotor,
    std::uint8_t smallMotor,
    std::uint8_t /* led_number */,
    void *userdata
  );

  /**
   * @brief Handle DualShock 4 virtual gamepad notification events.
   *
   * @param client ViGEm client.
   * @param target ViGEm target.
   * @param largeMotor Large motor strength.
   * @param smallMotor Small motor strength.
   * @param led_color Requested lightbar color.
   * @param userdata User data pointer.
   */
  void CALLBACK ds4_notify(
    client_t::pointer client,
    target_t::pointer target,
    std::uint8_t largeMotor,
    std::uint8_t smallMotor,
    DS4_LIGHTBAR_COLOR /* led_color */,
    void *userdata
  );

  /**
   * @brief Last touch coordinates reported by a virtual gamepad.
   */
  struct gp_touch_context_t {
    uint8_t pointerIndex;  ///< Pointer index.
    uint16_t x;  ///< X.
    uint16_t y;  ///< Y.
  };

  /**
   * @brief ViGEm target and report buffers for one virtual gamepad.
   */
  struct gamepad_context_t {
    target_t gp;  ///< Gp.
    feedback_queue_t feedback_queue;  ///< Feedback queue.

    union {
      XUSB_REPORT x360;  ///< Current Xbox 360 controller report.
      DS4_REPORT_EX ds4;  ///< Current DualShock 4 controller report.
    } report;  ///< Current HID report for the virtual controller..

    // Map from pointer ID to pointer index
    std::map<uint32_t, uint8_t> pointer_id_map;  ///< Pointer ID map.
    std::byte available_pointers;  ///< Byte mask of available DualShock 4 touch slots.

    uint8_t client_relative_index;  ///< Client relative index.

    thread_pool_util::ThreadPool::task_id_t repeat_task {};  ///< Repeat task.
    std::chrono::steady_clock::time_point last_report_ts;  ///< Last report ts.

    gamepad_feedback_msg_t last_rumble;  ///< Last rumble.
    gamepad_feedback_msg_t last_rgb_led;  ///< Last RGB led.
  };

  constexpr float EARTH_G = 9.80665f;  ///< Meters per second squared represented by one gravity unit.

/**
 * @def MPS2_TO_DS4_ACCEL(x)
 * @brief Macro for MPS2 TO DS4 ACCEL.
 */
#define MPS2_TO_DS4_ACCEL(x) (int32_t) (((x) / EARTH_G) * 8192)
/**
 * @def DPS_TO_DS4_GYRO(x)
 * @brief Macro for DPS TO DS4 GYRO.
 */
#define DPS_TO_DS4_GYRO(x) (int32_t) ((x) * (1024 / 64))

/**
 * @def APPLY_CALIBRATION(val, bias, scale)
 * @brief Macro for APPLY CALIBRATION.
 */
#define APPLY_CALIBRATION(val, bias, scale) (int32_t) (((float) (val) + (bias)) / (scale))

  /**
   * @brief DS4 touch unused.
   */
  constexpr DS4_TOUCH ds4_touch_unused = {
    .bPacketCounter = 0,
    .bIsUpTrackingNum1 = 0x80,
    .bTouchData1 = {0x00, 0x00, 0x00},
    .bIsUpTrackingNum2 = 0x80,
    .bTouchData2 = {0x00, 0x00, 0x00},
  };

  // See https://github.com/ViGEm/ViGEmBus/blob/22835473d17fbf0c4d4bb2f2d42fd692b6e44df4/sys/Ds4Pdo.cpp#L153-L164
  /**
   * @brief DS4 report init ex.
   */
  constexpr DS4_REPORT_EX ds4_report_init_ex = {
    {{.bThumbLX = 0x80,
      .bThumbLY = 0x80,
      .bThumbRX = 0x80,
      .bThumbRY = 0x80,
      .wButtons = DS4_BUTTON_DPAD_NONE,
      .bSpecial = 0,
      .bTriggerL = 0,
      .bTriggerR = 0,
      .wTimestamp = 0,
      .bBatteryLvl = 0xFF,
      .wGyroX = 0,
      .wGyroY = 0,
      .wGyroZ = 0,
      .wAccelX = 0,
      .wAccelY = 0,
      .wAccelZ = 0,
      ._bUnknown1 = {0x00, 0x00, 0x00, 0x00, 0x00},
      .bBatteryLvlSpecial = 0x1A,  // Wired - Full battery
      ._bUnknown2 = {0x00, 0x00},
      .bTouchPacketsN = 1,
      .sCurrentTouch = ds4_touch_unused,
      .sPreviousTouch = {ds4_touch_unused, ds4_touch_unused}}}
  };

  /**
   * @brief Updates the DS4 input report with the provided motion data.
   * @details Acceleration is in m/s^2 and gyro is in deg/s.
   * @param gamepad The gamepad to update.
   * @param motion_type The type of motion data.
   * @param x X component of motion.
   * @param y Y component of motion.
   * @param z Z component of motion.
   */
  static void ds4_update_motion(gamepad_context_t &gamepad, uint8_t motion_type, float x, float y, float z) {
    auto &report = gamepad.report.ds4.Report;

    // Use int32 to process this data, so we can clamp if needed.
    int32_t intX;
    int32_t intY;
    int32_t intZ;

    switch (motion_type) {
      case LI_MOTION_TYPE_ACCEL:
        // Convert to the DS4's accelerometer scale
        intX = MPS2_TO_DS4_ACCEL(x);
        intY = MPS2_TO_DS4_ACCEL(y);
        intZ = MPS2_TO_DS4_ACCEL(z);

        // Apply the inverse of ViGEmBus's calibration data
        intX = APPLY_CALIBRATION(intX, -297, 1.010796f);
        intY = APPLY_CALIBRATION(intY, -42, 1.014614f);
        intZ = APPLY_CALIBRATION(intZ, -512, 1.024768f);
        break;
      case LI_MOTION_TYPE_GYRO:
        // Convert to the DS4's gyro scale
        intX = DPS_TO_DS4_GYRO(x);
        intY = DPS_TO_DS4_GYRO(y);
        intZ = DPS_TO_DS4_GYRO(z);

        // Apply the inverse of ViGEmBus's calibration data
        intX = APPLY_CALIBRATION(intX, 1, 0.977596f);
        intY = APPLY_CALIBRATION(intY, 0, 0.972370f);
        intZ = APPLY_CALIBRATION(intZ, 0, 0.971550f);
        break;
      default:
        return;
    }

    // Clamp the values to the range of the data type
    intX = std::clamp(intX, INT16_MIN, INT16_MAX);
    intY = std::clamp(intY, INT16_MIN, INT16_MAX);
    intZ = std::clamp(intZ, INT16_MIN, INT16_MAX);

    // Populate the report
    switch (motion_type) {
      case LI_MOTION_TYPE_ACCEL:
        report.wAccelX = (int16_t) intX;
        report.wAccelY = (int16_t) intY;
        report.wAccelZ = (int16_t) intZ;
        break;
      case LI_MOTION_TYPE_GYRO:
        report.wGyroX = (int16_t) intX;
        report.wGyroY = (int16_t) intY;
        report.wGyroZ = (int16_t) intZ;
        break;
      default:
        return;
    }
  }

  /**
   * @brief ViGEm client connection and virtual gamepad collection.
   */
  class vigem_t {
  public:
    /**
     * @brief Connect to ViGEm and prepare virtual gamepad slots.
     *
     * @return 0 on success; nonzero or negative platform status on failure.
     */
    int init() {
      // Probe ViGEm during startup to see if we can successfully attach gamepads. This will allow us to
      // immediately display the error message in the web UI even before the user tries to stream.
      client_t probe_client {vigem_alloc()};
      VIGEM_ERROR status = vigem_connect(probe_client.get());
      if (!VIGEM_SUCCESS(status)) {
        BOOST_LOG(warning) << "ViGEmBus is not installed or running, so its limited Xbox 360 and DualShock 4 gamepad support is unavailable"sv;
        return -1;
      } else {
        vigem_disconnect(probe_client.get());
      }

      gamepads.resize(MAX_GAMEPADS);

      return 0;
    }

    /**
     * @brief Attaches a new gamepad.
     * @param id The gamepad ID.
     * @param feedback_queue The queue for posting messages back to the client.
     * @param gp_type The type of gamepad.
     * @return 0 on success.
     */
    int alloc_gamepad_internal(const gamepad_id_t &id, feedback_queue_t &feedback_queue, VIGEM_TARGET_TYPE gp_type) {
      auto &gamepad = gamepads[id.globalIndex];
      assert(!gamepad.gp);

      gamepad.client_relative_index = id.clientRelativeIndex;
      gamepad.last_report_ts = std::chrono::steady_clock::now();

      // Establish a connect to the ViGEm driver if we don't have one yet
      if (!client) {
        BOOST_LOG(debug) << "Connecting to ViGEmBus driver"sv;
        client.reset(vigem_alloc());

        auto status = vigem_connect(client.get());
        if (!VIGEM_SUCCESS(status)) {
          BOOST_LOG(warning) << "Couldn't setup connection to ViGEm for gamepad support ["sv << util::hex(status).to_string_view() << ']';
          client.reset();
          return -1;
        }
      }

      if (gp_type == Xbox360Wired) {
        gamepad.gp.reset(vigem_target_x360_alloc());
        XUSB_REPORT_INIT(&gamepad.report.x360);
      } else {
        gamepad.gp.reset(vigem_target_ds4_alloc());

        // There is no equivalent DS4_REPORT_EX_INIT()
        gamepad.report.ds4 = ds4_report_init_ex;

        // Set initial accelerometer and gyro state
        ds4_update_motion(gamepad, LI_MOTION_TYPE_ACCEL, 0.0f, EARTH_G, 0.0f);
        ds4_update_motion(gamepad, LI_MOTION_TYPE_GYRO, 0.0f, 0.0f, 0.0f);

        // Request motion events from the client at 100 Hz
        feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(gamepad.client_relative_index, LI_MOTION_TYPE_ACCEL, 100));
        feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(gamepad.client_relative_index, LI_MOTION_TYPE_GYRO, 100));

        // We support pointer index 0 and 1
        gamepad.available_pointers = std::byte {0x3};
      }

      auto status = vigem_target_add(client.get(), gamepad.gp.get());
      if (!VIGEM_SUCCESS(status)) {
        BOOST_LOG(error) << "Couldn't add Gamepad to ViGEm connection ["sv << util::hex(status).to_string_view() << ']';

        return -1;
      }

      gamepad.feedback_queue = std::move(feedback_queue);

      if (gp_type == Xbox360Wired) {
        status = vigem_target_x360_register_notification(client.get(), gamepad.gp.get(), x360_notify, this);
      } else {
        status = vigem_target_ds4_register_notification(client.get(), gamepad.gp.get(), ds4_notify, this);
      }

      if (!VIGEM_SUCCESS(status)) {
        BOOST_LOG(warning) << "Couldn't register notifications for rumble support ["sv << util::hex(status).to_string_view() << ']';
      }

      return 0;
    }

    /**
     * @brief Route an existing ViGEm gamepad's feedback to a resumed client.
     *
     * @param id Global and client-relative identifiers for the resumed controller.
     * @param feedback_queue Queue used to return gamepad feedback to the resumed client.
     * @return 0 when the target exists and was rebound; otherwise -1.
     */
    int rebind_gamepad(const gamepad_id_t &id, feedback_queue_t feedback_queue) {
      if (id.globalIndex < 0 || id.globalIndex >= static_cast<int>(gamepads.size())) {
        return -1;
      }

      auto &gamepad = gamepads[id.globalIndex];
      if (!gamepad.gp || !vigem_target_is_attached(gamepad.gp.get())) {
        return -1;
      }

      gamepad.client_relative_index = id.clientRelativeIndex;
      gamepad.feedback_queue = std::move(feedback_queue);
      gamepad.last_rumble = {};
      gamepad.last_rgb_led = {};

      if (gamepad.feedback_queue && vigem_target_get_type(gamepad.gp.get()) == DualShock4Wired) {
        gamepad.feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(id.clientRelativeIndex, LI_MOTION_TYPE_ACCEL, 100));
        gamepad.feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(id.clientRelativeIndex, LI_MOTION_TYPE_GYRO, 100));
      }

      return 0;
    }

    /**
     * @brief Detaches the specified gamepad
     * @param nr The gamepad.
     */
    void free_target(int nr) {
      auto &gamepad = gamepads[nr];

      if (gamepad.repeat_task) {
        task_pool.cancel(gamepad.repeat_task);
        gamepad.repeat_task = nullptr;
      }

      if (gamepad.gp && vigem_target_is_attached(gamepad.gp.get())) {
        auto status = vigem_target_remove(client.get(), gamepad.gp.get());
        if (!VIGEM_SUCCESS(status)) {
          BOOST_LOG(warning) << "Couldn't detach gamepad from ViGEm ["sv << util::hex(status).to_string_view() << ']';
        }
      }

      gamepad.gp.reset();

      // Disconnect from ViGEm if we just removed the last gamepad
      bool disconnect = true;
      for (auto &attached_gamepad : gamepads) {
        if (attached_gamepad.gp && vigem_target_is_attached(attached_gamepad.gp.get())) {
          disconnect = false;
          break;
        }
      }
      if (disconnect) {
        BOOST_LOG(debug) << "Disconnecting from ViGEmBus driver"sv;
        vigem_disconnect(client.get());
        client.reset();
      }
    }

    /**
     * @brief Pass rumble data back to the client.
     * @param target The gamepad.
     * @param largeMotor The large motor.
     * @param smallMotor The small motor.
     */
    void rumble(target_t::pointer target, std::uint8_t largeMotor, std::uint8_t smallMotor) {
      for (int x = 0; x < gamepads.size(); ++x) {
        auto &gamepad = gamepads[x];

        if (gamepad.gp.get() == target) {
          // Convert from 8-bit to 16-bit values
          const auto normalizedLargeMotor = vigem::scale_rumble(largeMotor);
          // Don't resend duplicate rumble data
          if (const auto normalizedSmallMotor = vigem::scale_rumble(smallMotor); normalizedSmallMotor != gamepad.last_rumble.data.rumble.highfreq || normalizedLargeMotor != gamepad.last_rumble.data.rumble.lowfreq) {
            // We have to use the client-relative index when communicating back to the client
            gamepad_feedback_msg_t msg = gamepad_feedback_msg_t::make_rumble(
              gamepad.client_relative_index,
              normalizedLargeMotor,
              normalizedSmallMotor
            );
            gamepad.feedback_queue->raise(msg);
            gamepad.last_rumble = msg;
          }
          return;
        }
      }
    }

    /**
     * @brief Pass RGB LED data back to the client.
     * @param target The gamepad.
     * @param r The red channel.
     * @param g The red channel.
     * @param b The red channel.
     */
    void set_rgb_led(target_t::pointer target, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
      for (int x = 0; x < gamepads.size(); ++x) {
        auto &gamepad = gamepads[x];

        if (gamepad.gp.get() == target) {
          // Don't resend duplicate RGB data
          if (r != gamepad.last_rgb_led.data.rgb_led.r || g != gamepad.last_rgb_led.data.rgb_led.g || b != gamepad.last_rgb_led.data.rgb_led.b) {
            // We have to use the client-relative index when communicating back to the client
            gamepad_feedback_msg_t msg = gamepad_feedback_msg_t::make_rgb_led(gamepad.client_relative_index, r, g, b);
            gamepad.feedback_queue->raise(msg);
            gamepad.last_rgb_led = msg;
          }
          return;
        }
      }
    }

    /**
     * @brief Detach all virtual gamepads and disconnect from the ViGEm client.
     */
    ~vigem_t() {
      if (client) {
        for (auto &gamepad : gamepads) {
          if (gamepad.gp && vigem_target_is_attached(gamepad.gp.get())) {
            auto status = vigem_target_remove(client.get(), gamepad.gp.get());
            if (!VIGEM_SUCCESS(status)) {
              BOOST_LOG(warning) << "Couldn't detach gamepad from ViGEm ["sv << util::hex(status).to_string_view() << ']';
            }
          }
        }

        vigem_disconnect(client.get());
      }
    }

    std::vector<gamepad_context_t> gamepads;  ///< Virtual gamepads owned by this ViGEm connection.

    client_t client;  ///< ViGEm client connection used to create virtual gamepads.
  };

  void CALLBACK x360_notify(
    client_t::pointer client,
    target_t::pointer target,
    std::uint8_t largeMotor,
    std::uint8_t smallMotor,
    std::uint8_t /* led_number */,
    void *userdata  // NOSONAR(cpp:S5008): ViGEm notification callbacks require a void pointer for user data.
  ) {
    BOOST_LOG(debug)
      << "largeMotor: "sv << (int) largeMotor << std::endl
      << "smallMotor: "sv << (int) smallMotor;

    task_pool.push(&vigem_t::rumble, (vigem_t *) userdata, target, largeMotor, smallMotor);
  }

  void CALLBACK ds4_notify(
    client_t::pointer client,
    target_t::pointer target,
    std::uint8_t largeMotor,
    std::uint8_t smallMotor,
    DS4_LIGHTBAR_COLOR led_color,
    void *userdata  // NOSONAR(cpp:S5008): ViGEm notification callbacks require a void pointer for user data.
  ) {
    BOOST_LOG(debug)
      << "largeMotor: "sv << (int) largeMotor << std::endl
      << "smallMotor: "sv << (int) smallMotor << std::endl
      << "LED: "sv << util::hex(led_color.Red).to_string_view() << ' '
      << util::hex(led_color.Green).to_string_view() << ' '
      << util::hex(led_color.Blue).to_string_view() << std::endl;

    task_pool.push(&vigem_t::rumble, (vigem_t *) userdata, target, largeMotor, smallMotor);
    task_pool.push(&vigem_t::set_rgb_led, (vigem_t *) userdata, target, led_color.Red, led_color.Green, led_color.Blue);
  }

  /**
   * @brief Global input backend handles shared by clients.
   */
  struct input_raw_t {
    std::unique_ptr<vigem_t> vigem;  ///< ViGEm gamepad context.
  };

  /**
   * @brief Return the gamepad choices supported by ViGEmBus.
   *
   * @param available Whether Solstice can connect to ViGEmBus.
   * @return Automatic, Xbox 360, and DualShock 4 choices with availability metadata.
   */
  std::vector<supported_gamepad_t> vigembus_supported_gamepads(const bool available) {
    const auto reason = available ? "" : "gamepads.vigembus-not-available";
    return {
      {"auto", available, reason},
      {"x360", available, reason},
      {"ds4", available, reason},
    };
  }

  input_t input() {
    input_t result {new input_raw_t {}};

    if (auto &raw = *result; config::input.controller) {
      auto vigem = std::make_unique<vigem_t>();
      if (!vigem->init()) {
        raw.vigem = std::move(vigem);
      }
    }

    return result;
  }

  /**
   * @brief Create the ViGEm context if it is not already available.
   *
   * @param raw Platform input context.
   * @return True when ViGEm is available.
   */
  bool ensure_vigem(input_raw_t *raw) {
    if (raw->vigem) {
      return true;
    }

    auto vigem = std::make_unique<vigem_t>();
    if (vigem->init()) {
      return false;
    }

    raw->vigem = std::move(vigem);
    return true;
  }

  /**
   * @brief Submit an input event through SendInput.
   *
   * @param input_event Keyboard or mouse event to submit.
   */
  void submit_input(INPUT &input_event) {
    if (SendInput(1, &input_event, sizeof(INPUT)) == 0) {
      BOOST_LOG(warning) << "SendInput failed with error ["sv << GetLastError() << ']';
    }
  }

  /**
   * @brief Submit a keyboard scancode event.
   *
   * @param modcode Virtual key code reported by the client.
   * @param release Whether this is a key release.
   * @param extended Whether the client identified an extended key.
   */
  void send_keyboard_scancode(const uint16_t modcode, const bool release, const bool extended) {
    const auto scancode = static_cast<WORD>(MapVirtualKeyW(modcode, MAPVK_VK_TO_VSC));

    INPUT input_event {};
    input_event.type = INPUT_KEYBOARD;
    input_event.ki.wScan = scancode;
    input_event.ki.dwFlags = KEYEVENTF_SCANCODE;
    if (extended) {
      input_event.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    if (release) {
      input_event.ki.dwFlags |= KEYEVENTF_KEYUP;
    }

    submit_input(input_event);
  }

  std::optional<util::point_t> get_mouse_loc(input_t & /*input*/) {
    POINT p;
    if (!GetCursorPos(&p)) {
      return std::nullopt;
    }

    return util::point_t {
      (double) p.x,
      (double) p.y
    };
  }

  /**
   * @brief Per-client native touch and pen injection state.
   */
  struct client_input_raw_t: public client_input_t {
    /**
     * @brief Create per-client native pointer state.
     *
     * @param input Platform input backend that receives the event.
     */
    explicit client_input_raw_t(input_t &) {}

    /**
     * @brief Destroy the synthetic pen device.
     */
    ~client_input_raw_t() override {
      if (pen_device) {
        DestroySyntheticPointerDevice(pen_device);
      }
    }

    std::uint32_t next_touch_frame {};  ///< Frame counter reported to the touch injector.
    HSYNTHETICPOINTERDEVICE pen_device {};  ///< Synthetic pen device used for pen events.
  };

  /**
   * @brief Convert a normalized client touch coordinate to a desktop pixel coordinate.
   *
   * @param touch_port Current stream viewport and desktop bounds.
   * @param x Horizontal normalized coordinate.
   * @param y Vertical normalized coordinate.
   * @return Desktop pixel coordinate.
   */
  POINT normalized_to_desktop(const touch_port_t &touch_port, const float x, const float y) {
    const auto width = touch_port.width ? touch_port.width : 1;
    const auto height = touch_port.height ? touch_port.height : 1;
    return {
      touch_port.env_offset_x + touch_port.offset_x + static_cast<LONG>(x * width),
      touch_port.env_offset_y + touch_port.offset_y + static_cast<LONG>(y * height),
    };
  }

  std::unique_ptr<client_input_t> allocate_client_input_context(input_t &input) {
    return std::make_unique<client_input_raw_t>(input);
  }

  int alloc_gamepad(input_t &input, const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue) {
    auto raw = (input_raw_t *) input.get();

    if (!ensure_vigem(raw)) {
      return -1;
    }

    VIGEM_TARGET_TYPE selectedGamepadType;

    if (config::input.gamepad == "x360"sv) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox 360 controller (manual selection)"sv;
      selectedGamepadType = Xbox360Wired;
    } else if (config::input.gamepad == "ds4"sv) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (manual selection)"sv;
      selectedGamepadType = DualShock4Wired;
    } else if (metadata.type == LI_CTYPE_PS) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (auto-selected by client-reported type)"sv;
      selectedGamepadType = DualShock4Wired;
    } else if (metadata.type == LI_CTYPE_XBOX) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox 360 controller (auto-selected by client-reported type)"sv;
      selectedGamepadType = Xbox360Wired;
    } else if (config::input.motion_as_ds4 && (metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO))) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (auto-selected by motion sensor presence)"sv;
      selectedGamepadType = DualShock4Wired;
    } else if (config::input.touchpad_as_ds4 && (metadata.capabilities & LI_CCAP_TOUCHPAD)) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (auto-selected by touchpad presence)"sv;
      selectedGamepadType = DualShock4Wired;
    } else {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox 360 controller (default)"sv;
      selectedGamepadType = Xbox360Wired;
    }

    if (selectedGamepadType == Xbox360Wired) {
      if (metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO)) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has motion sensors, but they are not usable when emulating an Xbox 360 controller"sv;
      }
      if (metadata.capabilities & LI_CCAP_TOUCHPAD) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has a touchpad, but it is not usable when emulating an Xbox 360 controller"sv;
      }
      if (metadata.capabilities & LI_CCAP_RGB_LED) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has an RGB LED, but it is not usable when emulating an Xbox 360 controller"sv;
      }
    } else if (selectedGamepadType == DualShock4Wired) {
      if (!(metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO))) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " is emulating a DualShock 4 controller, but the client gamepad doesn't have motion sensors active"sv;
      }
      if (!(metadata.capabilities & LI_CCAP_TOUCHPAD)) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " is emulating a DualShock 4 controller, but the client gamepad doesn't have a touchpad"sv;
      }
    }

    return raw->vigem->alloc_gamepad_internal(id, feedback_queue, selectedGamepadType);
  }

  int rebind_gamepad(input_t &input, const gamepad_id_t &id, feedback_queue_t feedback_queue) {
    auto raw = (input_raw_t *) input.get();

    if (!raw->vigem) {
      return -1;
    }

    return raw->vigem->rebind_gamepad(id, std::move(feedback_queue));
  }

  void free_gamepad(input_t &input, int nr) {
    auto raw = (input_raw_t *) input.get();

    if (!raw->vigem) {
      return;
    }

    raw->vigem->free_target(nr);
  }

  /**
   * @brief Converts the standard button flags into X360 format.
   * @param gamepad_state The gamepad button/axis state sent from the client.
   * @return XUSB_BUTTON flags.
   */
  static XUSB_BUTTON x360_buttons(const gamepad_state_t &gamepad_state) {
    int buttons {};

    auto flags = gamepad_state.buttonFlags;
    if (flags & DPAD_UP) {
      buttons |= XUSB_GAMEPAD_DPAD_UP;
    }
    if (flags & DPAD_DOWN) {
      buttons |= XUSB_GAMEPAD_DPAD_DOWN;
    }
    if (flags & DPAD_LEFT) {
      buttons |= XUSB_GAMEPAD_DPAD_LEFT;
    }
    if (flags & DPAD_RIGHT) {
      buttons |= XUSB_GAMEPAD_DPAD_RIGHT;
    }
    if (flags & START) {
      buttons |= XUSB_GAMEPAD_START;
    }
    if (flags & BACK) {
      buttons |= XUSB_GAMEPAD_BACK;
    }
    if (flags & LEFT_STICK) {
      buttons |= XUSB_GAMEPAD_LEFT_THUMB;
    }
    if (flags & RIGHT_STICK) {
      buttons |= XUSB_GAMEPAD_RIGHT_THUMB;
    }
    if (flags & LEFT_BUTTON) {
      buttons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    }
    if (flags & RIGHT_BUTTON) {
      buttons |= XUSB_GAMEPAD_RIGHT_SHOULDER;
    }
    if (flags & (HOME | MISC_BUTTON)) {
      buttons |= XUSB_GAMEPAD_GUIDE;
    }
    if (flags & A) {
      buttons |= XUSB_GAMEPAD_A;
    }
    if (flags & B) {
      buttons |= XUSB_GAMEPAD_B;
    }
    if (flags & X) {
      buttons |= XUSB_GAMEPAD_X;
    }
    if (flags & Y) {
      buttons |= XUSB_GAMEPAD_Y;
    }

    return (XUSB_BUTTON) buttons;
  }

  /**
   * @brief Update an X360 report from Solstice gamepad state.
   * @param gamepad The gamepad to update.
   * @param gamepad_state The gamepad button/axis state sent from the client.
   */
  static void x360_update_state(gamepad_context_t &gamepad, const gamepad_state_t &gamepad_state) {
    auto &report = gamepad.report.x360;

    report.wButtons = x360_buttons(gamepad_state);
    report.bLeftTrigger = gamepad_state.lt;
    report.bRightTrigger = gamepad_state.rt;
    report.sThumbLX = gamepad_state.lsX;
    report.sThumbLY = gamepad_state.lsY;
    report.sThumbRX = gamepad_state.rsX;
    report.sThumbRY = gamepad_state.rsY;
  }

  static DS4_DPAD_DIRECTIONS ds4_dpad(const gamepad_state_t &gamepad_state) {
    if (auto flags = gamepad_state.buttonFlags; flags & DPAD_UP) {
      if (flags & DPAD_RIGHT) {
        return DS4_BUTTON_DPAD_NORTHEAST;
      } else if (flags & DPAD_LEFT) {
        return DS4_BUTTON_DPAD_NORTHWEST;
      } else {
        return DS4_BUTTON_DPAD_NORTH;
      }
    }

    else if (flags & DPAD_DOWN) {
      if (flags & DPAD_RIGHT) {
        return DS4_BUTTON_DPAD_SOUTHEAST;
      } else if (flags & DPAD_LEFT) {
        return DS4_BUTTON_DPAD_SOUTHWEST;
      } else {
        return DS4_BUTTON_DPAD_SOUTH;
      }
    }

    else if (flags & DPAD_RIGHT) {
      return DS4_BUTTON_DPAD_EAST;
    }

    else if (flags & DPAD_LEFT) {
      return DS4_BUTTON_DPAD_WEST;
    }

    return DS4_BUTTON_DPAD_NONE;
  }

  /**
   * @brief Converts the standard button flags into DS4 format.
   * @param gamepad_state The gamepad button/axis state sent from the client.
   * @return DS4_BUTTONS flags.
   */
  static DS4_BUTTONS ds4_buttons(const gamepad_state_t &gamepad_state) {
    int buttons {};

    auto flags = gamepad_state.buttonFlags;
    if (flags & LEFT_STICK) {
      buttons |= DS4_BUTTON_THUMB_LEFT;
    }
    if (flags & RIGHT_STICK) {
      buttons |= DS4_BUTTON_THUMB_RIGHT;
    }
    if (flags & LEFT_BUTTON) {
      buttons |= DS4_BUTTON_SHOULDER_LEFT;
    }
    if (flags & RIGHT_BUTTON) {
      buttons |= DS4_BUTTON_SHOULDER_RIGHT;
    }
    if (flags & START) {
      buttons |= DS4_BUTTON_OPTIONS;
    }
    if (flags & BACK) {
      buttons |= DS4_BUTTON_SHARE;
    }
    if (flags & A) {
      buttons |= DS4_BUTTON_CROSS;
    }
    if (flags & B) {
      buttons |= DS4_BUTTON_CIRCLE;
    }
    if (flags & X) {
      buttons |= DS4_BUTTON_SQUARE;
    }
    if (flags & Y) {
      buttons |= DS4_BUTTON_TRIANGLE;
    }

    if (gamepad_state.lt > 0) {
      buttons |= DS4_BUTTON_TRIGGER_LEFT;
    }
    if (gamepad_state.rt > 0) {
      buttons |= DS4_BUTTON_TRIGGER_RIGHT;
    }

    return (DS4_BUTTONS) buttons;
  }

  static DS4_SPECIAL_BUTTONS ds4_special_buttons(const gamepad_state_t &gamepad_state) {
    int buttons {};

    if (gamepad_state.buttonFlags & HOME) {
      buttons |= DS4_SPECIAL_BUTTON_PS;
    }

    // Allow either PS4/PS5 clickpad button or Xbox Series X share button to activate DS4 clickpad
    if (gamepad_state.buttonFlags & (TOUCHPAD_BUTTON | MISC_BUTTON)) {
      buttons |= DS4_SPECIAL_BUTTON_TOUCHPAD;
    }

    // PlayStation-style emulation: check if BACK should also trigger touchpad click
    if (config::input.ds4_back_as_touchpad_click && (gamepad_state.buttonFlags & BACK)) {
      buttons |= DS4_SPECIAL_BUTTON_TOUCHPAD;
    }

    return (DS4_SPECIAL_BUTTONS) buttons;
  }

  static std::uint8_t to_ds4_triggerX(std::int16_t v) {
    return (v + std::numeric_limits<std::uint16_t>::max() / 2 + 1) / 257;
  }

  static std::uint8_t to_ds4_triggerY(std::int16_t v) {
    auto new_v = -((std::numeric_limits<std::uint16_t>::max() / 2 + v - 1)) / 257;

    return new_v == 0 ? 0xFF : (std::uint8_t) new_v;
  }

  /**
   * @brief Update a DS4 report from Solstice gamepad state.
   * @param gamepad The gamepad to update.
   * @param gamepad_state The gamepad button/axis state sent from the client.
   */
  static void ds4_update_state(gamepad_context_t &gamepad, const gamepad_state_t &gamepad_state) {
    auto &report = gamepad.report.ds4.Report;

    report.wButtons = static_cast<uint16_t>(ds4_buttons(gamepad_state)) | static_cast<uint16_t>(ds4_dpad(gamepad_state));
    report.bSpecial = ds4_special_buttons(gamepad_state);

    report.bTriggerL = gamepad_state.lt;
    report.bTriggerR = gamepad_state.rt;

    report.bThumbLX = to_ds4_triggerX(gamepad_state.lsX);
    report.bThumbLY = to_ds4_triggerY(gamepad_state.lsY);

    report.bThumbRX = to_ds4_triggerX(gamepad_state.rsX);
    report.bThumbRY = to_ds4_triggerY(gamepad_state.rsY);
  }

  /**
   * @brief Sends DS4 input with updated timestamps and repeats to keep timestamp updated.
   * @details Some applications require updated timestamps values to register DS4 input.
   * @param vigem The global ViGEm context object.
   * @param nr The global gamepad index.
   */
  void ds4_update_ts_and_send(vigem_t *vigem, int nr) {
    auto &gamepad = vigem->gamepads[nr];

    // Cancel any pending updates. We will requeue one here when we're finished.
    if (gamepad.repeat_task) {
      task_pool.cancel(gamepad.repeat_task);
      gamepad.repeat_task = nullptr;
    }

    if (gamepad.gp && vigem_target_is_attached(gamepad.gp.get())) {
      auto now = std::chrono::steady_clock::now();
      auto delta_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - gamepad.last_report_ts);

      // Timestamp is reported in 5.333us units
      gamepad.report.ds4.Report.wTimestamp += (uint16_t) (delta_ns.count() / 5333);

      // Send the report to the virtual device
      auto status = vigem_target_ds4_update_ex(vigem->client.get(), gamepad.gp.get(), gamepad.report.ds4);
      if (!VIGEM_SUCCESS(status)) {
        BOOST_LOG(warning) << "Couldn't send gamepad input to ViGEm ["sv << util::hex(status).to_string_view() << ']';
        return;
      }

      // Repeat at least every 100ms to keep the 16-bit timestamp field from overflowing
      gamepad.last_report_ts = now;
      gamepad.repeat_task = task_pool.pushDelayed(ds4_update_ts_and_send, 100ms, vigem, nr).task_id;
    }
  }

  /**
   * @brief Submit updated Solstice gamepad state to the virtual device.
   * @param input The input context.
   * @param nr The gamepad index to update.
   * @param gamepad_state The gamepad button/axis state sent from the client.
   */
  void gamepad_update(input_t &input, int nr, const gamepad_state_t &gamepad_state) {
    auto raw = (input_raw_t *) input.get();

    auto *vigem = raw->vigem.get();

    // If there is no gamepad support
    if (!vigem) {
      return;
    }

    auto &gamepad = vigem->gamepads[nr];
    if (!gamepad.gp) {
      return;
    }

    VIGEM_ERROR status;

    if (vigem_target_get_type(gamepad.gp.get()) == Xbox360Wired) {
      x360_update_state(gamepad, gamepad_state);
      status = vigem_target_x360_update(vigem->client.get(), gamepad.gp.get(), gamepad.report.x360);
      if (!VIGEM_SUCCESS(status)) {
        BOOST_LOG(warning) << "Couldn't send gamepad input to ViGEm ["sv << util::hex(status).to_string_view() << ']';
      }
    } else {
      ds4_update_state(gamepad, gamepad_state);
      ds4_update_ts_and_send(vigem, nr);
    }
  }

  std::uint16_t vigem::scale_rumble(std::uint8_t strength) {
    return strength * 256;
  }

  bool vigem::update_touch(DS4_REPORT_EX &ds4_report, std::map<std::uint32_t, std::uint8_t> &pointer_id_map, std::byte &available_pointers, const gamepad_touch_t &touch) {
    auto &report = ds4_report.Report;

    uint8_t pointerIndex {};
    if (touch.eventType == LI_TOUCH_EVENT_DOWN) {
      if ((available_pointers & std::byte {0x1}) != std::byte {0}) {
        // Reserve pointer index 0 for this touch
        pointer_id_map[touch.pointerId] = pointerIndex = 0;
        available_pointers &= ~(std::byte {1} << pointerIndex);

        // Set pointer 0 down
        report.sCurrentTouch.bIsUpTrackingNum1 = std::to_integer<uint8_t>(std::byte {report.sCurrentTouch.bIsUpTrackingNum1} & ~std::byte {0x80});
        report.sCurrentTouch.bIsUpTrackingNum1++;
      } else if ((available_pointers & std::byte {0x2}) != std::byte {0}) {
        // Reserve pointer index 1 for this touch
        pointer_id_map[touch.pointerId] = pointerIndex = 1;
        available_pointers &= ~(std::byte {1} << pointerIndex);

        // Set pointer 1 down
        report.sCurrentTouch.bIsUpTrackingNum2 = std::to_integer<uint8_t>(std::byte {report.sCurrentTouch.bIsUpTrackingNum2} & ~std::byte {0x80});
        report.sCurrentTouch.bIsUpTrackingNum2++;
      } else {
        BOOST_LOG(warning) << "No more free pointer indices! Did the client miss an touch up event?"sv;
        return false;
      }
    } else if (touch.eventType == LI_TOUCH_EVENT_CANCEL_ALL) {
      // Raise both pointers
      report.sCurrentTouch.bIsUpTrackingNum1 = std::to_integer<uint8_t>(std::byte {report.sCurrentTouch.bIsUpTrackingNum1} | std::byte {0x80});
      report.sCurrentTouch.bIsUpTrackingNum2 = std::to_integer<uint8_t>(std::byte {report.sCurrentTouch.bIsUpTrackingNum2} | std::byte {0x80});

      // Remove all pointer index mappings
      pointer_id_map.clear();

      // All pointers are now available
      available_pointers = std::byte {0x3};
    } else {
      auto i = pointer_id_map.find(touch.pointerId);
      if (i == pointer_id_map.end()) {
        BOOST_LOG(warning) << "Pointer ID not found! Did the client miss a touch down event?"sv;
        return false;
      }

      pointerIndex = (*i).second;

      if (touch.eventType == LI_TOUCH_EVENT_UP || touch.eventType == LI_TOUCH_EVENT_CANCEL) {
        // Remove the pointer index mapping
        pointer_id_map.erase(i);

        // Set pointer up
        if (pointerIndex == 0) {
          report.sCurrentTouch.bIsUpTrackingNum1 = std::to_integer<uint8_t>(std::byte {report.sCurrentTouch.bIsUpTrackingNum1} | std::byte {0x80});
        } else {
          report.sCurrentTouch.bIsUpTrackingNum2 = std::to_integer<uint8_t>(std::byte {report.sCurrentTouch.bIsUpTrackingNum2} | std::byte {0x80});
        }

        // Free the pointer index
        available_pointers |= (std::byte {1} << pointerIndex);
      } else if (touch.eventType != LI_TOUCH_EVENT_MOVE) {
        BOOST_LOG(warning) << "Unsupported touch event for gamepad: "sv << (uint32_t) touch.eventType;
        return false;
      }
    }

    // Touchpad is 1920x943 according to ViGEm
    uint16_t x = touch.x * 1920;
    uint16_t y = touch.y * 943;
    std::array<std::byte, 3> touchData = {
      static_cast<std::byte>(x & 0xFF),  // Low 8 bits of X
      static_cast<std::byte>((x >> 8 & 0x0F) | (y & 0x0F) << 4),  // High 4 bits of X and low 4 bits of Y
      static_cast<std::byte>(y >> 4 & 0xFF)  // High 8 bits of Y
    };

    report.sCurrentTouch.bPacketCounter++;
    if (touch.eventType != LI_TOUCH_EVENT_CANCEL_ALL) {
      if (pointerIndex == 0) {
        memcpy(report.sCurrentTouch.bTouchData1, touchData.data(), touchData.size());
      } else {
        memcpy(report.sCurrentTouch.bTouchData2, touchData.data(), touchData.size());
      }
    }

    return true;
  }

  void vigem::update_battery(DS4_REPORT_EX &ds4_report, const gamepad_battery_t &battery) {
    // For details on the report format of these battery level fields, see:
    // https://github.com/torvalds/linux/blob/946c6b59c56dc6e7d8364a8959cb36bf6d10bc37/drivers/hid/hid-playstation.c#L2305-L2314

    auto &report = ds4_report.Report;
    auto battery_status = std::byte {report.bBatteryLvlSpecial};

    // Update the battery state if it is known
    switch (battery.state) {
      case LI_BATTERY_STATE_CHARGING:
      case LI_BATTERY_STATE_DISCHARGING:
        if (battery.state == LI_BATTERY_STATE_CHARGING) {
          battery_status |= std::byte {0x10};  // Connected via USB
        } else {
          battery_status &= ~std::byte {0x10};  // Not connected via USB
        }

        // If there was a special battery status set before, clear that and
        // initialize the battery level to 50%. It will be overwritten below
        // if the actual percentage is known.
        if ((battery_status & std::byte {0xF}) > std::byte {0xA}) {
          battery_status = (battery_status & ~std::byte {0xF}) | std::byte {0x5};
        }
        break;

      case LI_BATTERY_STATE_FULL:
        battery_status = std::byte {0x1B};  // USB + Battery Full
        report.bBatteryLvl = 0xFF;
        break;

      case LI_BATTERY_STATE_NOT_PRESENT:
      case LI_BATTERY_STATE_NOT_CHARGING:
        battery_status = std::byte {0x1F};  // USB + Charging Error
        break;

      default:
        break;
    }

    // Update the battery level if it is known
    if (battery.percentage != LI_BATTERY_PERCENTAGE_UNKNOWN) {
      report.bBatteryLvl = battery.percentage * 255 / 100;

      // Don't overwrite low nibble if there's a special status there (see above)
      if ((battery_status & std::byte {0x10}) != std::byte {0} && (battery_status & std::byte {0xF}) <= std::byte {0xA}) {
        battery_status = (battery_status & ~std::byte {0xF}) | static_cast<std::byte>((battery.percentage + 5) / 10);
      }
    }

    report.bBatteryLvlSpecial = std::to_integer<uint8_t>(battery_status);
  }

  /**
   * @brief Sends a gamepad touch event to the OS.
   * @param input The global input context.
   * @param touch The touch event.
   */
  void gamepad_touch(input_t &input, const gamepad_touch_t &touch) {
    auto raw = (input_raw_t *) input.get();

    auto *vigem = raw->vigem.get();

    // If there is no gamepad support
    if (!vigem) {
      return;
    }

    auto &gamepad = vigem->gamepads[touch.id.globalIndex];
    if (!gamepad.gp) {
      return;
    }

    // Touch is only supported on DualShock 4 controllers
    if (vigem_target_get_type(gamepad.gp.get()) != DualShock4Wired) {
      return;
    }

    if (vigem::update_touch(gamepad.report.ds4, gamepad.pointer_id_map, gamepad.available_pointers, touch)) {
      ds4_update_ts_and_send(vigem, touch.id.globalIndex);
    }
  }

  /**
   * @brief Sends a gamepad motion event to the OS.
   * @param input The global input context.
   * @param motion The motion event.
   */
  void gamepad_motion(input_t &input, const gamepad_motion_t &motion) {
    auto raw = (input_raw_t *) input.get();

    auto *vigem = raw->vigem.get();

    // If there is no gamepad support
    if (!vigem) {
      return;
    }

    auto &gamepad = vigem->gamepads[motion.id.globalIndex];
    if (!gamepad.gp) {
      return;
    }

    // Motion is only supported on DualShock 4 controllers
    if (vigem_target_get_type(gamepad.gp.get()) != DualShock4Wired) {
      return;
    }

    ds4_update_motion(gamepad, motion.motionType, motion.x, motion.y, motion.z);
    ds4_update_ts_and_send(vigem, motion.id.globalIndex);
  }

  /**
   * @brief Sends a gamepad battery event to the OS.
   * @param input The global input context.
   * @param battery The battery event.
   */
  void gamepad_battery(input_t &input, const gamepad_battery_t &battery) {
    auto raw = (input_raw_t *) input.get();

    auto *vigem = raw->vigem.get();

    // If there is no gamepad support
    if (!vigem) {
      return;
    }

    auto &gamepad = vigem->gamepads[battery.id.globalIndex];
    if (!gamepad.gp) {
      return;
    }

    // Battery is only supported on DualShock 4 controllers
    if (vigem_target_get_type(gamepad.gp.get()) != DualShock4Wired) {
      return;
    }

    vigem::update_battery(gamepad.report.ds4, battery);
    ds4_update_ts_and_send(vigem, battery.id.globalIndex);
  }

  void freeInput(input_raw_t *input) {
    std::default_delete<input_raw_t> {}(input);
  }

  std::vector<supported_gamepad_t> &supported_gamepads(input_t *input) {
    static std::vector<supported_gamepad_t> gps;
    if (!input || !input->get()) {
      gps = vigembus_supported_gamepads(false);
      return gps;
    }

    const auto raw = (input_raw_t *) input->get();
    gps = vigembus_supported_gamepads(raw->vigem != nullptr);
    return gps;
  }

  void move_mouse(input_t & /*input*/, const int deltaX, const int deltaY) {
    INPUT input_event {};
    input_event.type = INPUT_MOUSE;
    input_event.mi.dx = deltaX;
    input_event.mi.dy = deltaY;
    input_event.mi.dwFlags = MOUSEEVENTF_MOVE;
    submit_input(input_event);
  }

  void move_mouse(input_t &input, const touch_port_t & /*touch_port*/, const int deltaX, const int deltaY) {
    move_mouse(input, deltaX, deltaY);
  }

  void abs_mouse(input_t & /*input*/, const touch_port_t &touch_port, const float x, const float y) {
    // MOUSEEVENTF_VIRTUALDESK normalizes the coordinates against the virtual desktop, so the
    // display origin and the virtual desktop extents are both needed to reach the right pixel.
    const auto virtual_x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const auto virtual_y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const auto virtual_width = std::max(GetSystemMetrics(SM_CXVIRTUALSCREEN), 1);
    const auto virtual_height = std::max(GetSystemMetrics(SM_CYVIRTUALSCREEN), 1);
    const auto desktop_x = touch_port.env_offset_x + touch_port.offset_x + x * touch_port.width;
    const auto desktop_y = touch_port.env_offset_y + touch_port.offset_y + y * touch_port.height;

    INPUT input_event {};
    input_event.type = INPUT_MOUSE;
    input_event.mi.dx = static_cast<LONG>(std::lround((desktop_x - virtual_x) * 65535.0 / virtual_width));
    input_event.mi.dy = static_cast<LONG>(std::lround((desktop_y - virtual_y) * 65535.0 / virtual_height));
    input_event.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    submit_input(input_event);
  }

  void button_mouse(input_t & /*input*/, const int button, const bool release) {
    DWORD flags {};
    DWORD mouse_data {};

    switch (button) {
      case 0x01:
        flags = release ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN;
        break;
      case 0x02:
        flags = release ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_MIDDLEDOWN;
        break;
      case 0x03:
        flags = release ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_RIGHTDOWN;
        break;
      case 0x04:
        flags = release ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN;
        mouse_data = XBUTTON1;
        break;
      case 0x05:
        flags = release ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN;
        mouse_data = XBUTTON2;
        break;
      default:
        return;
    }

    INPUT input_event {};
    input_event.type = INPUT_MOUSE;
    input_event.mi.dwFlags = flags;
    input_event.mi.mouseData = mouse_data;
    submit_input(input_event);
  }

  void scroll(input_t & /*input*/, const int distance) {
    INPUT input_event {};
    input_event.type = INPUT_MOUSE;
    input_event.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input_event.mi.mouseData = static_cast<DWORD>(distance);
    submit_input(input_event);
  }

  void hscroll(input_t & /*input*/, const int distance) {
    INPUT input_event {};
    input_event.type = INPUT_MOUSE;
    input_event.mi.dwFlags = MOUSEEVENTF_HWHEEL;
    input_event.mi.mouseData = static_cast<DWORD>(distance);
    submit_input(input_event);
  }

  void keyboard_update(input_t & /*input*/, const uint16_t modcode, const bool release, uint8_t /*flags*/, const bool extended) {
    if (modcode == VK_LWIN || modcode == VK_RWIN || modcode == VK_APPS) {
      // Windows key and the context menu key must be injected as extended keys.
      send_keyboard_scancode(modcode, release, true);
      return;
    }

    send_keyboard_scancode(modcode, release, extended);
  }

  void unicode(input_t & /*input*/, const char *utf8, const int size) {
    // Convert UTF-8 to UTF-16 and inject each code unit as a Unicode key event.
    const auto utf16 = utf_utils::from_utf8(std::string {utf8, static_cast<std::size_t>(size)});
    for (const auto code_unit : utf16) {
      INPUT input_event {};
      input_event.type = INPUT_KEYBOARD;
      input_event.ki.wScan = code_unit;
      input_event.ki.dwFlags = KEYEVENTF_UNICODE;
      submit_input(input_event);

      input_event.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
      submit_input(input_event);
    }
  }

  void touch_update(client_input_t *input, const touch_port_t &touch_port, const touch_input_t &touch) {
    auto raw = static_cast<client_input_raw_t *>(input);
    if (!raw) {
      return;
    }

    static std::once_flag touch_init_flag;
    static bool touch_available = false;
    std::call_once(touch_init_flag, []() {
      touch_available = InitializeTouchInjection(10, TOUCH_FEEDBACK_DEFAULT) != FALSE;
      if (!touch_available) {
        BOOST_LOG(warning) << "Native touch injection is unavailable ["sv << GetLastError() << ']';
      }
    });
    if (!touch_available) {
      return;
    }

    POINTER_TOUCH_INFO contact {};
    contact.pointerInfo.pointerType = PT_TOUCH;
    contact.pointerInfo.pointerId = touch.pointerId;
    contact.pointerInfo.ptPixelLocation = normalized_to_desktop(touch_port, touch.x, touch.y);
    contact.pointerInfo.pointerFlags = POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
    if (touch.eventType == LI_TOUCH_EVENT_DOWN) {
      contact.pointerInfo.pointerFlags |= POINTER_FLAG_DOWN;
    } else if (touch.eventType == LI_TOUCH_EVENT_UP || touch.eventType == LI_TOUCH_EVENT_CANCEL) {
      contact.pointerInfo.pointerFlags |= POINTER_FLAG_UP;
    } else if (touch.eventType == LI_TOUCH_EVENT_CANCEL_ALL) {
      // Lifting the contact is the closest native equivalent of a cancel-all event.
      contact.pointerInfo.pointerFlags |= POINTER_FLAG_UP;
    } else {
      contact.pointerInfo.pointerFlags |= POINTER_FLAG_UPDATE;
    }

    contact.touchFlags = TOUCH_FLAG_NONE;
    contact.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
    const auto half_width = static_cast<LONG>(std::max(touch.contactAreaMajor, 0.0f) / 2.0f);
    const auto half_height = static_cast<LONG>(std::max(touch.contactAreaMinor, 0.0f) / 2.0f);
    contact.rcContact = {
      contact.pointerInfo.ptPixelLocation.x - half_width,
      contact.pointerInfo.ptPixelLocation.y - half_height,
      contact.pointerInfo.ptPixelLocation.x + half_width,
      contact.pointerInfo.ptPixelLocation.y + half_height,
    };
    contact.orientation = touch.rotation;
    contact.pressure = static_cast<UINT32>(std::clamp(touch.pressureOrDistance, 0.0f, 1.0f) * 1024);

    if (!InjectTouchInput(1, &contact)) {
      BOOST_LOG(warning) << "InjectTouchInput failed with error ["sv << GetLastError() << ']';
    }
  }

  void pen_update(client_input_t *input, const touch_port_t &touch_port, const pen_input_t &pen) {
    auto raw = static_cast<client_input_raw_t *>(input);
    if (!raw) {
      return;
    }

    if (!raw->pen_device) {
      raw->pen_device = CreateSyntheticPointerDevice(PT_PEN, 1, POINTER_FEEDBACK_DEFAULT);
      if (!raw->pen_device) {
        BOOST_LOG(warning) << "Native pen injection is unavailable ["sv << GetLastError() << ']';
        return;
      }
    }

    POINTER_TYPE_INFO pointer_info {};
    pointer_info.type = PT_PEN;
    auto &pen_info = pointer_info.penInfo;
    pen_info.pointerInfo.pointerType = PT_PEN;
    pen_info.pointerInfo.pointerId = 1;
    pen_info.pointerInfo.ptPixelLocation = normalized_to_desktop(touch_port, pen.x, pen.y);
    pen_info.pointerInfo.pointerFlags = POINTER_FLAG_INRANGE;
    if (pen.eventType == LI_TOUCH_EVENT_DOWN) {
      pen_info.pointerInfo.pointerFlags |= POINTER_FLAG_INCONTACT | POINTER_FLAG_DOWN;
    } else if (pen.eventType == LI_TOUCH_EVENT_UP || pen.eventType == LI_TOUCH_EVENT_CANCEL) {
      pen_info.pointerInfo.pointerFlags |= POINTER_FLAG_UP;
    } else {
      pen_info.pointerInfo.pointerFlags |= POINTER_FLAG_UPDATE;
    }

    if (pen.penButtons & LI_PEN_BUTTON_PRIMARY) {
      pen_info.pointerInfo.pointerFlags |= POINTER_FLAG_FIRSTBUTTON;
    }
    if (pen.penButtons & LI_PEN_BUTTON_SECONDARY) {
      pen_info.pointerInfo.pointerFlags |= POINTER_FLAG_SECONDBUTTON;
    }
    if (pen.penButtons & LI_PEN_BUTTON_TERTIARY) {
      pen_info.pointerInfo.pointerFlags |= POINTER_FLAG_THIRDBUTTON;
    }

    pen_info.penMask = PEN_MASK_PRESSURE | PEN_MASK_ROTATION | PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
    pen_info.pressure = static_cast<UINT32>(std::clamp(pen.pressureOrDistance, 0.0f, 1.0f) * 1024);
    pen_info.rotation = pen.rotation;
    pen_info.tiltX = pen.tilt;
    pen_info.tiltY = 0;

    if (!InjectSyntheticPointerInput(raw->pen_device, &pointer_info, 1)) {
      BOOST_LOG(warning) << "InjectSyntheticPointerInput failed with error ["sv << GetLastError() << ']';
    }
  }

  /**
   * @brief Returns the supported platform capabilities to advertise to the client.
   * @return Capability flags.
   */
  platform_caps::caps_t get_capabilities() {
    platform_caps::caps_t caps = 0;

    if (config::input.controller) {
      caps |= platform_caps::controller_touch;
    }

    if (config::input.native_pen_touch) {
      caps |= platform_caps::pen_touch;
    }

    return caps;
  }
}  // namespace platf
