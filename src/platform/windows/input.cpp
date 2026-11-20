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
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

// lib includes
#include <padbus/padbus.hpp>

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
   * @brief Meters per second squared represented by one gravity unit.
   */
  constexpr float EARTH_G = 9.80665f;

  namespace gamepad {
    /**
     * @brief Raw DualShock 4 accelerometer units per gravity.
     */
    constexpr float DS4_ACCEL_PER_G = 8192.0f;

    /**
     * @brief Raw DualShock 4 gyroscope units per degree per second.
     */
    constexpr float DS4_GYRO_PER_DPS = 1024.0f / 64.0f;

    /**
     * @brief Clamp an intermediate sensor value to the report's integer range.
     *
     * @param value Sensor value in raw DualShock 4 units.
     * @return Value clamped to the signed 16-bit range.
     */
    static std::int32_t clamp_report_value(float value) {
      return static_cast<std::int32_t>(std::clamp(value, static_cast<float>(std::numeric_limits<std::int16_t>::min()), static_cast<float>(std::numeric_limits<std::int16_t>::max())));
    }

    float normalize_stick(std::int16_t value) {
      return std::clamp(static_cast<float>(value) / std::numeric_limits<std::int16_t>::max(), -1.0f, 1.0f);
    }

    float normalize_trigger(std::uint8_t value) {
      return static_cast<float>(value) / std::numeric_limits<std::uint8_t>::max();
    }

    padbus::ButtonSet buttons(std::uint32_t flags, bool dualshock4, bool back_as_touchpad_click) {
      padbus::ButtonSet result;

      const auto press = [&result, flags](std::uint32_t mask, padbus::GamepadButton button) {
        if (flags & mask) {
          result.set(button);
        }
      };

      press(DPAD_UP, padbus::GamepadButton::dpad_up);
      press(DPAD_DOWN, padbus::GamepadButton::dpad_down);
      press(DPAD_LEFT, padbus::GamepadButton::dpad_left);
      press(DPAD_RIGHT, padbus::GamepadButton::dpad_right);
      press(START, padbus::GamepadButton::start);
      press(BACK, padbus::GamepadButton::back);
      press(LEFT_STICK, padbus::GamepadButton::left_stick);
      press(RIGHT_STICK, padbus::GamepadButton::right_stick);
      press(LEFT_BUTTON, padbus::GamepadButton::left_shoulder);
      press(RIGHT_BUTTON, padbus::GamepadButton::right_shoulder);
      press(HOME, padbus::GamepadButton::guide);
      press(A, padbus::GamepadButton::a);
      press(B, padbus::GamepadButton::b);
      press(X, padbus::GamepadButton::x);
      press(Y, padbus::GamepadButton::y);

      if (dualshock4) {
        // Allow either PS4/PS5 clickpad button or Xbox Series X share button to activate the clickpad
        press(TOUCHPAD_BUTTON | MISC_BUTTON, padbus::GamepadButton::touchpad);
        if (back_as_touchpad_click) {
          // PlayStation-style emulation: the back button also clicks the touchpad
          press(BACK, padbus::GamepadButton::touchpad);
        }
      } else {
        press(MISC_BUTTON, padbus::GamepadButton::guide);
      }

      return result;
    }

    bool update_touch(std::array<padbus::GamepadTouchContact, 2> &contacts, std::map<std::uint32_t, std::uint8_t> &pointer_id_map, const gamepad_touch_t &touch) {
      std::uint8_t pointer_index {};

      if (touch.eventType == LI_TOUCH_EVENT_DOWN) {
        const auto free_slot = std::find_if(contacts.begin(), contacts.end(), [](const padbus::GamepadTouchContact &contact) {
          return !contact.active;
        });

        if (free_slot == contacts.end()) {
          BOOST_LOG(warning) << "No more free pointer indices! Did the client miss an touch up event?"sv;
          return false;
        }

        pointer_index = static_cast<std::uint8_t>(std::distance(contacts.begin(), free_slot));
        pointer_id_map[touch.pointerId] = pointer_index;
      } else if (touch.eventType == LI_TOUCH_EVENT_CANCEL_ALL) {
        // Raise both contacts without discarding the last reported coordinates
        for (auto &contact : contacts) {
          contact.active = false;
        }
        pointer_id_map.clear();
        return true;
      } else {
        const auto mapping = pointer_id_map.find(touch.pointerId);
        if (mapping == pointer_id_map.end()) {
          BOOST_LOG(warning) << "Pointer ID not found! Did the client miss a touch down event?"sv;
          return false;
        }

        pointer_index = mapping->second;

        if (touch.eventType == LI_TOUCH_EVENT_UP || touch.eventType == LI_TOUCH_EVENT_CANCEL) {
          pointer_id_map.erase(mapping);
          contacts[pointer_index].active = false;
        } else if (touch.eventType != LI_TOUCH_EVENT_MOVE) {
          BOOST_LOG(warning) << "Unsupported touch event for gamepad: "sv << static_cast<std::uint32_t>(touch.eventType);
          return false;
        }
      }

      auto &contact = contacts[pointer_index];
      contact.id = pointer_index;
      contact.active = touch.eventType != LI_TOUCH_EVENT_UP && touch.eventType != LI_TOUCH_EVENT_CANCEL;
      contact.x = std::clamp(touch.x, 0.0f, 1.0f);
      contact.y = std::clamp(touch.y, 0.0f, 1.0f);

      return true;
    }

    std::optional<padbus::GamepadBattery> battery(const gamepad_battery_t &battery) {
      const auto percentage = battery.percentage == LI_BATTERY_PERCENTAGE_UNKNOWN
                                ? std::nullopt
                                : std::optional<std::uint8_t> {std::min<std::uint8_t>(battery.percentage, 100)};

      switch (battery.state) {
        case LI_BATTERY_STATE_CHARGING:
          return padbus::GamepadBattery {percentage.value_or(100), true};
        case LI_BATTERY_STATE_DISCHARGING:
          return padbus::GamepadBattery {percentage.value_or(0), false};
        case LI_BATTERY_STATE_FULL:
          return padbus::GamepadBattery {100, true};
        case LI_BATTERY_STATE_NOT_PRESENT:
        case LI_BATTERY_STATE_NOT_CHARGING:
          return std::nullopt;
        default:
          if (!percentage) {
            return std::nullopt;
          }
          return padbus::GamepadBattery {*percentage, false};
      }
    }

    padbus::Vector3 ds4_acceleration(float x, float y, float z) {
      // Apply the inverse of the driver's calibration data so the emulated sensor matches the client
      const auto calibrate = [](float value, float bias, float scale) {
        const auto raw = static_cast<float>((value / EARTH_G) * DS4_ACCEL_PER_G);
        return static_cast<float>(clamp_report_value((raw + bias) / scale));
      };

      return {
        calibrate(x, -297, 1.010796f),
        calibrate(y, -42, 1.014614f),
        calibrate(z, -512, 1.024768f),
      };
    }

    padbus::Vector3 ds4_gyroscope(float x, float y, float z) {
      const auto calibrate = [](float value, float bias, float scale) {
        const auto raw = static_cast<float>(value * DS4_GYRO_PER_DPS);
        return static_cast<float>(clamp_report_value((raw + bias) / scale));
      };

      return {
        calibrate(x, 1, 0.977596f),
        calibrate(y, 0, 0.972370f),
        calibrate(z, 0, 0.971550f),
      };
    }
  }  // namespace gamepad

  /**
   * @brief Describe a PadBus operation failure for the log.
   *
   * @param status Status returned by PadBus.
   * @return Human-readable status text.
   */
  static std::string status_text(const padbus::OperationStatus &status) {
    std::string text = padbus::to_string(status.code());
    if (!status.message().empty()) {
      text += ": "sv;
      text += status.message();
    }
    if (status.has_native_code()) {
      text += " (native code "sv;
      text += std::to_string(status.native_code());
      text += ')';
    }
    return text;
  }

  /**
   * @brief Create the runtime options used by the Windows input backend.
   *
   * @return Runtime options that route PadBus diagnostics into the Solstice log.
   */
  static padbus::RuntimeOptions runtime_options() {
    padbus::RuntimeOptions options;
    options.log_callback = [](const padbus::LogLevel level, const std::string &message) {
      switch (level) {
        case padbus::LogLevel::debug:
          BOOST_LOG(debug) << message;
          break;
        case padbus::LogLevel::info:
          BOOST_LOG(info) << message;
          break;
        case padbus::LogLevel::warning:
          BOOST_LOG(warning) << message;
          break;
        case padbus::LogLevel::error:
          BOOST_LOG(error) << message;
          break;
      }
    };
    return options;
  }

  /**
   * @brief PadBus target and state for one virtual gamepad.
   */
  struct gamepad_context_t {
    std::unique_ptr<padbus::Gamepad> gp;  ///< Virtual gamepad owned by the backend.
    bool dualshock4 {};  ///< Whether the gamepad emulates a DualShock 4.
    feedback_queue_t feedback_queue;  ///< Feedback queue.

    padbus::GamepadState state;  ///< State submitted with the next report.
    std::map<uint32_t, uint8_t> pointer_id_map;  ///< Map from pointer ID to touchpad contact slot.

    uint8_t client_relative_index;  ///< Client relative index.

    thread_pool_util::ThreadPool::task_id_t repeat_task {};  ///< Repeat task.
    std::chrono::steady_clock::time_point last_report_ts;  ///< Last report ts.

    gamepad_feedback_msg_t last_rumble;  ///< Last rumble.
    gamepad_feedback_msg_t last_rgb_led;  ///< Last RGB led.
  };

  /**
   * @brief PadBus runtime connection and virtual gamepad collection.
   */
  class padbus_backend_t {
  public:
    /**
     * @brief Create the runtime and probe whether gamepads can be attached.
     *
     * The probe lets the Web UI report an unusable driver before the user tries to stream.
     *
     * @return 0 on success; nonzero platform status on failure.
     */
    int init() {
      if (!connect()) {
        BOOST_LOG(warning) << "PadBus is not installed or running, so its Xbox 360 and DualShock 4 gamepad support is unavailable"sv;
        return -1;
      }

      gamepads.resize(MAX_GAMEPADS);

      return 0;
    }

    /**
     * @brief Attaches a new gamepad.
     * @param id The gamepad ID.
     * @param feedback_queue The queue for posting messages back to the client.
     * @param dualshock4 Whether to emulate a DualShock 4 instead of an Xbox 360 controller.
     * @return 0 on success.
     */
    int alloc_gamepad_internal(const gamepad_id_t &id, feedback_queue_t &feedback_queue, bool dualshock4) {
      auto &gamepad = gamepads[id.globalIndex];
      assert(!gamepad.gp);

      gamepad.client_relative_index = id.clientRelativeIndex;
      gamepad.last_report_ts = std::chrono::steady_clock::now();
      gamepad.dualshock4 = dualshock4;
      gamepad.state = {};
      gamepad.pointer_id_map.clear();

      // Establish a connection to the driver if we don't have one yet
      if (!runtime && !connect()) {
        return -1;
      }

      const auto profile = dualshock4 ? padbus::profiles::dualshock4() : padbus::profiles::xbox_360();
      auto created = runtime->create_gamepad(profile);
      if (!created) {
        BOOST_LOG(error) << "Couldn't add gamepad to the PadBus runtime ["sv << status_text(created.status) << ']';
        return -1;
      }

      gamepad.gp = std::move(created.gamepad);

      if (dualshock4) {
        // Set initial accelerometer and gyro state
        gamepad.state.acceleration = gamepad::ds4_acceleration(0.0f, EARTH_G, 0.0f);
        gamepad.state.gyroscope = gamepad::ds4_gyroscope(0.0f, 0.0f, 0.0f);

        // Request motion events from the client at 100 Hz
        gamepad.feedback_queue = std::move(feedback_queue);
        gamepad.feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(gamepad.client_relative_index, LI_MOTION_TYPE_ACCEL, 100));
        gamepad.feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(gamepad.client_relative_index, LI_MOTION_TYPE_GYRO, 100));
      } else {
        gamepad.feedback_queue = std::move(feedback_queue);
      }

      gamepad.gp->set_output_callback([this, index = id.globalIndex](const padbus::GamepadOutput &output) {
        switch (output.kind) {
          case padbus::GamepadOutputKind::rumble:
            BOOST_LOG(debug) << "largeMotor: "sv << output.low_frequency_rumble << std::endl
                             << "smallMotor: "sv << output.high_frequency_rumble;
            task_pool.push(&padbus_backend_t::rumble, this, index, output.low_frequency_rumble, output.high_frequency_rumble);
            break;
          case padbus::GamepadOutputKind::rgb_led:
            task_pool.push(&padbus_backend_t::set_rgb_led, this, index, output.red, output.green, output.blue);
            break;
          default:
            break;
        }
      });

      return 0;
    }

    /**
     * @brief Route an existing gamepad's feedback to a resumed client.
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
      if (!gamepad.gp || !gamepad.gp->is_open()) {
        return -1;
      }

      if (!gamepad.gp->is_attached()) {
        // The driver dropped the target, for example after a bus restart
        const auto status = gamepad.gp->reattach();
        if (!status.ok() || !gamepad.gp->is_attached()) {
          BOOST_LOG(warning) << "Couldn't reattach gamepad to PadBus ["sv << status_text(status) << ']';
          return -1;
        }
      }

      gamepad.client_relative_index = id.clientRelativeIndex;
      gamepad.feedback_queue = std::move(feedback_queue);
      gamepad.last_rumble = {};
      gamepad.last_rgb_led = {};

      if (gamepad.feedback_queue && gamepad.dualshock4) {
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

      if (gamepad.gp) {
        if (const auto status = gamepad.gp->close(); !status.ok()) {
          BOOST_LOG(warning) << "Couldn't detach gamepad from PadBus ["sv << status_text(status) << ']';
        }
        gamepad.gp.reset();
      }

      // Drop the runtime if we just removed the last gamepad
      const auto attached = std::any_of(gamepads.begin(), gamepads.end(), [](const gamepad_context_t &other) {
        return other.gp != nullptr;
      });
      if (!attached) {
        BOOST_LOG(debug) << "Disconnecting from PadBus driver"sv;
        runtime.reset();
      }
    }

    /**
     * @brief Pass rumble data back to the client.
     * @param index The gamepad index.
     * @param low_frequency Low frequency motor strength.
     * @param high_frequency High frequency motor strength.
     */
    void rumble(int index, std::uint16_t low_frequency, std::uint16_t high_frequency) {
      auto &gamepad = gamepads[index];
      if (!gamepad.gp || !gamepad.feedback_queue) {
        return;
      }

      // Don't resend duplicate rumble data
      if (high_frequency == gamepad.last_rumble.data.rumble.highfreq && low_frequency == gamepad.last_rumble.data.rumble.lowfreq) {
        return;
      }

      // We have to use the client-relative index when communicating back to the client
      gamepad_feedback_msg_t msg = gamepad_feedback_msg_t::make_rumble(
        gamepad.client_relative_index,
        low_frequency,
        high_frequency
      );
      gamepad.feedback_queue->raise(msg);
      gamepad.last_rumble = msg;
    }

    /**
     * @brief Pass RGB LED data back to the client.
     * @param index The gamepad index.
     * @param r The red channel.
     * @param g The green channel.
     * @param b The blue channel.
     */
    void set_rgb_led(int index, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
      auto &gamepad = gamepads[index];
      if (!gamepad.gp || !gamepad.feedback_queue) {
        return;
      }

      // Don't resend duplicate RGB data
      if (r == gamepad.last_rgb_led.data.rgb_led.r && g == gamepad.last_rgb_led.data.rgb_led.g && b == gamepad.last_rgb_led.data.rgb_led.b) {
        return;
      }

      // We have to use the client-relative index when communicating back to the client
      gamepad_feedback_msg_t msg = gamepad_feedback_msg_t::make_rgb_led(gamepad.client_relative_index, r, g, b);
      gamepad.feedback_queue->raise(msg);
      gamepad.last_rgb_led = msg;
    }

    /**
     * @brief Submit the cached state of one gamepad.
     * @param index The gamepad index.
     */
    void submit(int index) {
      auto &gamepad = gamepads[index];
      if (!gamepad.gp) {
        return;
      }

      if (const auto status = gamepad.gp->submit(gamepad.state); !status.ok()) {
        BOOST_LOG(warning) << "Couldn't send gamepad input to PadBus ["sv << status_text(status) << ']';
        return;
      }

      gamepad.last_report_ts = std::chrono::steady_clock::now();

      // Repeat at least every 100ms to keep the DualShock 4 timestamp advancing
      if (gamepad.dualshock4) {
        if (gamepad.repeat_task) {
          task_pool.cancel(gamepad.repeat_task);
        }
        gamepad.repeat_task = task_pool.pushDelayed(&padbus_backend_t::repeat_submit, 100ms, this, index).task_id;
      }
    }

    /**
     * @brief Resubmit a DualShock 4 report so its timestamp keeps moving.
     * @param index The gamepad index.
     */
    void repeat_submit(int index) {
      gamepads[index].repeat_task = nullptr;
      submit(index);
    }

    /**
     * @brief Detach all virtual gamepads and destroy the runtime.
     */
    ~padbus_backend_t() {
      for (auto &gamepad : gamepads) {
        if (gamepad.repeat_task) {
          task_pool.cancel(gamepad.repeat_task);
          gamepad.repeat_task = nullptr;
        }
        gamepad.gp.reset();
      }
      runtime.reset();
    }

    std::vector<gamepad_context_t> gamepads;  ///< Virtual gamepads owned by this runtime.
    std::unique_ptr<padbus::Runtime> runtime;  ///< PadBus runtime used to create virtual gamepads.

  private:
    /**
     * @brief Create a PadBus runtime when the driver is available.
     *
     * @return True when the runtime can create devices.
     */
    bool connect() {
      BOOST_LOG(debug) << "Connecting to PadBus driver"sv;

      runtime = padbus::Runtime::create(runtime_options());
      if (!runtime || !runtime->capabilities().available) {
        runtime.reset();
        return false;
      }

      return true;
    }
  };

  /**
   * @brief Global input backend handles shared by clients.
   */
  struct input_raw_t {
    std::unique_ptr<padbus_backend_t> backend;  ///< PadBus gamepad context.
  };

  /**
   * @brief Return the gamepad choices supported by PadBus.
   *
   * @param available Whether Solstice can connect to PadBus.
   * @return Automatic, Xbox 360, and DualShock 4 choices with availability metadata.
   */
  std::vector<supported_gamepad_t> padbus_supported_gamepads(const bool available) {
    const auto reason = available ? "" : "gamepads.padbus-not-available";
    return {
      {"auto", available, reason},
      {"x360", available, reason},
      {"ds4", available, reason},
    };
  }

  input_t input() {
    input_t result {new input_raw_t {}};

    if (auto &raw = *result; config::input.controller) {
      auto backend = std::make_unique<padbus_backend_t>();
      if (!backend->init()) {
        raw.backend = std::move(backend);
      }
    }

    return result;
  }

  /**
   * @brief Create the PadBus context if it is not already available.
   *
   * @param raw Platform input context.
   * @return True when PadBus is available.
   */
  bool ensure_backend(input_raw_t *raw) {
    if (raw->backend) {
      return true;
    }

    auto backend = std::make_unique<padbus_backend_t>();
    if (backend->init()) {
      return false;
    }

    raw->backend = std::move(backend);
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

    if (!ensure_backend(raw)) {
      return -1;
    }

    bool dualshock4;

    if (config::input.gamepad == "x360"sv) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox 360 controller (manual selection)"sv;
      dualshock4 = false;
    } else if (config::input.gamepad == "ds4"sv) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (manual selection)"sv;
      dualshock4 = true;
    } else if (metadata.type == LI_CTYPE_PS) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (auto-selected by client-reported type)"sv;
      dualshock4 = true;
    } else if (metadata.type == LI_CTYPE_XBOX) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox 360 controller (auto-selected by client-reported type)"sv;
      dualshock4 = false;
    } else if (config::input.motion_as_ds4 && (metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO))) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (auto-selected by motion sensor presence)"sv;
      dualshock4 = true;
    } else if (config::input.touchpad_as_ds4 && (metadata.capabilities & LI_CCAP_TOUCHPAD)) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (auto-selected by touchpad presence)"sv;
      dualshock4 = true;
    } else {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox 360 controller (default)"sv;
      dualshock4 = false;
    }

    if (!dualshock4) {
      if (metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO)) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has motion sensors, but they are not usable when emulating an Xbox 360 controller"sv;
      }
      if (metadata.capabilities & LI_CCAP_TOUCHPAD) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has a touchpad, but it is not usable when emulating an Xbox 360 controller"sv;
      }
      if (metadata.capabilities & LI_CCAP_RGB_LED) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has an RGB LED, but it is not usable when emulating an Xbox 360 controller"sv;
      }
    } else {
      if (!(metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO))) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " is emulating a DualShock 4 controller, but the client gamepad doesn't have motion sensors active"sv;
      }
      if (!(metadata.capabilities & LI_CCAP_TOUCHPAD)) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " is emulating a DualShock 4 controller, but the client gamepad doesn't have a touchpad"sv;
      }
    }

    return raw->backend->alloc_gamepad_internal(id, feedback_queue, dualshock4);
  }

  int rebind_gamepad(input_t &input, const gamepad_id_t &id, feedback_queue_t feedback_queue) {
    auto raw = (input_raw_t *) input.get();

    if (!raw->backend) {
      return -1;
    }

    return raw->backend->rebind_gamepad(id, std::move(feedback_queue));
  }

  void free_gamepad(input_t &input, int nr) {
    auto raw = (input_raw_t *) input.get();

    if (!raw->backend) {
      return;
    }

    raw->backend->free_target(nr);
  }

  /**
   * @brief Submit updated Solstice gamepad state to the virtual device.
   * @param input The input context.
   * @param nr The gamepad index to update.
   * @param gamepad_state The gamepad button/axis state sent from the client.
   */
  void gamepad_update(input_t &input, int nr, const gamepad_state_t &gamepad_state) {
    auto raw = (input_raw_t *) input.get();

    auto *backend = raw->backend.get();

    // If there is no gamepad support
    if (!backend) {
      return;
    }

    auto &gamepad = backend->gamepads[nr];
    if (!gamepad.gp) {
      return;
    }

    gamepad.state.buttons = gamepad::buttons(gamepad_state.buttonFlags, gamepad.dualshock4, config::input.ds4_back_as_touchpad_click);
    gamepad.state.left_stick = {
      gamepad::normalize_stick(gamepad_state.lsX),
      gamepad::normalize_stick(gamepad_state.lsY),
    };
    gamepad.state.right_stick = {
      gamepad::normalize_stick(gamepad_state.rsX),
      gamepad::normalize_stick(gamepad_state.rsY),
    };
    gamepad.state.left_trigger = gamepad::normalize_trigger(gamepad_state.lt);
    gamepad.state.right_trigger = gamepad::normalize_trigger(gamepad_state.rt);

    backend->submit(nr);
  }

  /**
   * @brief Sends a gamepad touch event to the OS.
   * @param input The global input context.
   * @param touch The touch event.
   */
  void gamepad_touch(input_t &input, const gamepad_touch_t &touch) {
    auto raw = (input_raw_t *) input.get();

    auto *backend = raw->backend.get();

    // If there is no gamepad support
    if (!backend) {
      return;
    }

    auto &gamepad = backend->gamepads[touch.id.globalIndex];
    if (!gamepad.gp) {
      return;
    }

    // Touch is only supported on DualShock 4 controllers
    if (!gamepad.dualshock4) {
      return;
    }

    if (gamepad::update_touch(gamepad.state.touchpad_contacts, gamepad.pointer_id_map, touch)) {
      backend->submit(touch.id.globalIndex);
    }
  }

  /**
   * @brief Sends a gamepad motion event to the OS.
   * @param input The global input context.
   * @param motion The motion event.
   */
  void gamepad_motion(input_t &input, const gamepad_motion_t &motion) {
    auto raw = (input_raw_t *) input.get();

    auto *backend = raw->backend.get();

    // If there is no gamepad support
    if (!backend) {
      return;
    }

    auto &gamepad = backend->gamepads[motion.id.globalIndex];
    if (!gamepad.gp) {
      return;
    }

    // Motion is only supported on DualShock 4 controllers
    if (!gamepad.dualshock4) {
      return;
    }

    switch (motion.motionType) {
      case LI_MOTION_TYPE_ACCEL:
        gamepad.state.acceleration = gamepad::ds4_acceleration(motion.x, motion.y, motion.z);
        break;
      case LI_MOTION_TYPE_GYRO:
        gamepad.state.gyroscope = gamepad::ds4_gyroscope(motion.x, motion.y, motion.z);
        break;
      default:
        return;
    }

    backend->submit(motion.id.globalIndex);
  }

  /**
   * @brief Sends a gamepad battery event to the OS.
   * @param input The global input context.
   * @param battery The battery event.
   */
  void gamepad_battery(input_t &input, const gamepad_battery_t &battery) {
    auto raw = (input_raw_t *) input.get();

    auto *backend = raw->backend.get();

    // If there is no gamepad support
    if (!backend) {
      return;
    }

    auto &gamepad = backend->gamepads[battery.id.globalIndex];
    if (!gamepad.gp) {
      return;
    }

    // Battery is only supported on DualShock 4 controllers
    if (!gamepad.dualshock4) {
      return;
    }

    gamepad.state.battery = gamepad::battery(battery);
    backend->submit(battery.id.globalIndex);
  }

  void freeInput(input_raw_t *input) {
    std::default_delete<input_raw_t> {}(input);
  }

  std::vector<supported_gamepad_t> &supported_gamepads(input_t *input) {
    static std::vector<supported_gamepad_t> gps;
    if (!input || !input->get()) {
      gps = padbus_supported_gamepads(false);
      return gps;
    }

    const auto raw = (input_raw_t *) input->get();
    gps = padbus_supported_gamepads(raw->backend != nullptr);
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
