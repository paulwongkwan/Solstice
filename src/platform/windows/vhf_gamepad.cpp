/**
 * @file src/platform/windows/vhf_gamepad.cpp
 * @brief libvirtualgamepad backed Windows virtual gamepad backend.
 */

// platform includes
#include <Windows.h>

// standard includes
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <optional>
#include <utility>

// lib includes
#include <libvirtualgamepad/client.h>

// local includes
#include "vhf_gamepad.h"
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"

namespace platf {
  using namespace std::literals;

  namespace vhf {
    std::optional<lvg::profile> profile_from_name(std::string_view name) {
      if (name == "ds4"sv || name == "dualshock4"sv) {
        return lvg::profile::dualshock_4;
      }
      if (name == "dualsense"sv) {
        return lvg::profile::dualsense;
      }
      if (name == "xbox_one"sv) {
        return lvg::profile::xbox_one;
      }
      if (name == "xbox_series"sv) {
        return lvg::profile::xbox_series;
      }
      if (name == "switch_pro"sv) {
        return lvg::profile::switch_pro;
      }
      if (name == "steam_controller"sv) {
        return lvg::profile::steam_controller;
      }
      return std::nullopt;
    }

    std::string_view name_from_profile(lvg::profile profile) {
      switch (profile) {
        case lvg::profile::dualshock_4:
          return "ds4"sv;
        case lvg::profile::dualsense:
          return "dualsense"sv;
        case lvg::profile::xbox_one:
          return "xbox_one"sv;
        case lvg::profile::xbox_series:
          return "xbox_series"sv;
        case lvg::profile::switch_pro:
          return "switch_pro"sv;
        case lvg::profile::steam_controller:
          return "steam_controller"sv;
        default:
          return {};
      }
    }

    bool folds_playstation_state(lvg::profile profile) {
      return profile == lvg::profile::dualshock_4 || profile == lvg::profile::dualsense ||
             profile == lvg::profile::switch_pro || profile == lvg::profile::steam_controller;
    }

    std::uint32_t button_mask(std::uint32_t flags, bool playstation, bool back_as_touchpad_click) {
      std::uint32_t result = 0;
      const auto press = [&result, flags](std::uint32_t mask, lvg::button_mask button) {
        if (flags & mask) {
          result |= static_cast<std::uint32_t>(button);
        }
      };

      press(DPAD_UP, lvg::button_mask::dpad_up);
      press(DPAD_DOWN, lvg::button_mask::dpad_down);
      press(DPAD_LEFT, lvg::button_mask::dpad_left);
      press(DPAD_RIGHT, lvg::button_mask::dpad_right);
      press(START, lvg::button_mask::start);
      press(BACK, lvg::button_mask::back);
      press(LEFT_STICK, lvg::button_mask::left_stick);
      press(RIGHT_STICK, lvg::button_mask::right_stick);
      press(LEFT_BUTTON, lvg::button_mask::left_shoulder);
      press(RIGHT_BUTTON, lvg::button_mask::right_shoulder);
      press(HOME, lvg::button_mask::home);
      press(A, lvg::button_mask::south);
      press(B, lvg::button_mask::east);
      press(X, lvg::button_mask::west);
      press(Y, lvg::button_mask::north);
      press(TOUCHPAD_BUTTON, lvg::button_mask::touchpad);
      press(MISC_BUTTON, playstation ? lvg::button_mask::touchpad : lvg::button_mask::home);

      // PlayStation-style emulation: the back button also clicks the touchpad
      if (playstation && back_as_touchpad_click && (flags & BACK)) {
        result |= static_cast<std::uint32_t>(lvg::button_mask::touchpad);
      }

      return result;
    }

    std::optional<lvg::touch_event> map_touch_event(std::uint8_t event_type) {
      switch (event_type) {
        case LI_TOUCH_EVENT_HOVER:
          return lvg::touch_event::hover;
        case LI_TOUCH_EVENT_DOWN:
          return lvg::touch_event::down;
        case LI_TOUCH_EVENT_UP:
          return lvg::touch_event::up;
        case LI_TOUCH_EVENT_MOVE:
          return lvg::touch_event::move;
        case LI_TOUCH_EVENT_CANCEL:
          return lvg::touch_event::cancel;
        case LI_TOUCH_EVENT_CANCEL_ALL:
          return lvg::touch_event::cancel_all;
        default:
          return std::nullopt;
      }
    }

    std::uint16_t touch_coordinate(float value) {
      return static_cast<std::uint16_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 65535.0f));
    }

    lvg::battery_state battery_state(std::uint8_t state) {
      switch (state) {
        case LI_BATTERY_STATE_CHARGING:
          return lvg::battery_state::charging;
        case LI_BATTERY_STATE_DISCHARGING:
          return lvg::battery_state::discharging;
        case LI_BATTERY_STATE_FULL:
          return lvg::battery_state::full;
        case LI_BATTERY_STATE_NOT_PRESENT:
          return lvg::battery_state::not_present;
        case LI_BATTERY_STATE_NOT_CHARGING:
          return lvg::battery_state::not_charging;
        default:
          return lvg::battery_state::unknown;
      }
    }
  }  // namespace vhf

  void vhf_gamepad_t::initialize_header(lvg::request_header &header, std::size_t size) {
    header.size = static_cast<std::uint32_t>(size);
    header.version = lvg::k_protocol_version;
    header.reserved = 0;
  }

  int vhf_gamepad_t::init() {
    std::lock_guard lock {mutex_};

    slots_.clear();
    slots_.resize(MAX_GAMEPADS);

    const auto status = client_.connect();
    if (status != ERROR_SUCCESS) {
      BOOST_LOG(debug) << "libvirtualgamepad driver is not available ["sv << status << ']';
      return -1;
    }

    BOOST_LOG(info) << "Connected to the libvirtualgamepad driver with protocol version "sv << lvg::k_protocol_version;

    stop_polling_ = false;
    feedback_thread_ = std::thread {&vhf_gamepad_t::feedback_loop, this};
    polling_started_ = true;
    return 0;
  }

  bool vhf_gamepad_t::available() const noexcept {
    std::lock_guard lock {mutex_};
    return client_.connected();
  }

  lvg::profile_mask_t vhf_gamepad_t::available_profiles() const noexcept {
    std::lock_guard lock {mutex_};
    return client_.connected() ? client_.available_profiles() : 0u;
  }

  bool vhf_gamepad_t::supports(lvg::profile profile) const noexcept {
    return (available_profiles() & lvg::profile_bit(profile)) != 0;
  }

  int vhf_gamepad_t::alloc_gamepad(const gamepad_id_t &id, feedback_queue_t feedback_queue, lvg::profile profile) {
    if (id.globalIndex < 0 || id.globalIndex >= static_cast<int>(MAX_GAMEPADS)) {
      return -1;
    }

    std::lock_guard lock {mutex_};
    if (!client_.connected() || !supports(profile)) {
      return -1;
    }

    auto &slot = slots_[id.globalIndex];
    if (slot.created) {
      return -1;
    }

    // Creation can race the removal of a previous child in the same slot; the
    // driver reports ERROR_BUSY until PnP finishes, so retry with a short backoff.
    DWORD status = ERROR_BUSY;
    for (int attempt = 0; attempt < 5 && (status == ERROR_BUSY || status == ERROR_NOT_READY); ++attempt) {
      status = client_.create_controller(static_cast<std::uint32_t>(id.globalIndex), profile);
      if (status == ERROR_BUSY || status == ERROR_NOT_READY) {
        std::this_thread::sleep_for(20ms);
      }
    }
    if (status != ERROR_SUCCESS) {
      BOOST_LOG(warning) << "Couldn't create a "sv << vhf::name_from_profile(profile) << " gamepad ["sv << status << ']';
      return -1;
    }

    slot.created = true;
    slot.profile = profile;
    slot.client_relative_index = id.clientRelativeIndex;
    slot.feedback_queue = std::move(feedback_queue);
    slot.pointer_id_map.clear();
    slot.contacts = {};
    slot.last_report_ts = std::chrono::steady_clock::now();

    slot.input = {};
    initialize_header(slot.input.header, sizeof(slot.input));
    slot.input.controller_id = static_cast<std::uint32_t>(id.globalIndex);

    // Publish neutral axes so the device presents a defined state before the first client report.
    if (const auto neutralize = client_.submit_input_state(slot.input); neutralize != ERROR_SUCCESS) {
      BOOST_LOG(warning) << "Couldn't publish the initial gamepad state ["sv << neutralize << ']';
    }

    if (vhf::folds_playstation_state(profile) && slot.feedback_queue) {
      // Request motion events from the client at 100 Hz
      slot.feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(slot.client_relative_index, LI_MOTION_TYPE_ACCEL, 100));
      slot.feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(slot.client_relative_index, LI_MOTION_TYPE_GYRO, 100));
    }

    BOOST_LOG(info) << "Gamepad "sv << id.globalIndex << " is a "sv << vhf::name_from_profile(profile) << " controller through libvirtualgamepad"sv;
    return 0;
  }

  int vhf_gamepad_t::rebind_gamepad(const gamepad_id_t &id, feedback_queue_t feedback_queue) {
    std::lock_guard lock {mutex_};
    if (id.globalIndex < 0 || id.globalIndex >= static_cast<int>(MAX_GAMEPADS) || !slots_[id.globalIndex].created) {
      return -1;
    }

    auto &slot = slots_[id.globalIndex];
    slot.client_relative_index = id.clientRelativeIndex;
    slot.feedback_queue = std::move(feedback_queue);

    if (vhf::folds_playstation_state(slot.profile) && slot.feedback_queue) {
      slot.feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(slot.client_relative_index, LI_MOTION_TYPE_ACCEL, 100));
      slot.feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(slot.client_relative_index, LI_MOTION_TYPE_GYRO, 100));
    }

    return 0;
  }

  void vhf_gamepad_t::free_gamepad(int nr) {
    if (nr < 0 || nr >= static_cast<int>(MAX_GAMEPADS)) {
      return;
    }

    std::lock_guard lock {mutex_};
    auto &slot = slots_[nr];
    if (!slot.created) {
      return;
    }

    const auto status = client_.destroy_controller(static_cast<std::uint32_t>(nr));
    if (status != ERROR_SUCCESS) {
      BOOST_LOG(warning) << "Couldn't remove the virtual gamepad ["sv << status << ']';
    }

    slot = {};
  }

  bool vhf_gamepad_t::attached(int nr) const {
    std::lock_guard lock {mutex_};
    return nr >= 0 && nr < static_cast<int>(MAX_GAMEPADS) && slots_[nr].created;
  }

  void vhf_gamepad_t::update_gamepad(int nr, const gamepad_state_t &state) {
    if (nr < 0 || nr >= static_cast<int>(MAX_GAMEPADS)) {
      return;
    }

    std::lock_guard lock {mutex_};
    auto &slot = slots_[nr];
    if (!slot.created) {
      return;
    }

    slot.input.buttons = vhf::button_mask(state.buttonFlags, vhf::folds_playstation_state(slot.profile), config::input.ds4_back_as_touchpad_click);
    slot.input.left_x = state.lsX;
    slot.input.left_y = state.lsY;
    slot.input.right_x = state.rsX;
    slot.input.right_y = state.rsY;
    slot.input.left_trigger = state.lt;
    slot.input.right_trigger = state.rt;

    if (const auto status = client_.submit_input_state(slot.input); status != ERROR_SUCCESS) {
      BOOST_LOG(warning) << "Couldn't send gamepad input to libvirtualgamepad ["sv << status << ']';
      return;
    }

    slot.last_report_ts = std::chrono::steady_clock::now();
  }

  void vhf_gamepad_t::update_touch(const gamepad_touch_t &touch) {
    const auto event = vhf::map_touch_event(touch.eventType);
    if (!event || touch.id.globalIndex < 0 || touch.id.globalIndex >= static_cast<int>(MAX_GAMEPADS)) {
      return;
    }

    std::lock_guard lock {mutex_};
    auto &slot = slots_[touch.id.globalIndex];
    if (!slot.created || !vhf::folds_playstation_state(slot.profile)) {
      return;
    }

    // Only the two contacts the driver exposes are addressable.
    if (*event == lvg::touch_event::cancel_all) {
      for (std::size_t index = 0; index < slot.contacts.size(); ++index) {
        if (!slot.contacts[index]) {
          continue;
        }

        lvg::touch_state_request request {};
        initialize_header(request.header, sizeof(request));
        request.controller_id = static_cast<std::uint32_t>(touch.id.globalIndex);
        request.contact_index = static_cast<std::uint8_t>(index);
        request.event_type = static_cast<std::uint8_t>(lvg::touch_event::up);
        if (const auto status = client_.submit_touch_state(request); status != ERROR_SUCCESS) {
          BOOST_LOG(debug) << "Couldn't release a gamepad touch contact ["sv << status << ']';
        }
        slot.contacts[index] = false;
      }

      slot.pointer_id_map.clear();
      return;
    }

    std::uint8_t contact_index {};
    if (*event == lvg::touch_event::down || *event == lvg::touch_event::hover) {
      const auto free_slot = std::find(slot.contacts.begin(), slot.contacts.end(), false);
      if (free_slot == slot.contacts.end()) {
        BOOST_LOG(warning) << "No more free touch contacts! Did the client miss a touch up event?"sv;
        return;
      }

      contact_index = static_cast<std::uint8_t>(std::distance(slot.contacts.begin(), free_slot));
      slot.pointer_id_map[touch.pointerId] = contact_index;
      slot.contacts[contact_index] = *event == lvg::touch_event::down;
    } else {
      const auto found = slot.pointer_id_map.find(touch.pointerId);
      if (found == slot.pointer_id_map.end()) {
        BOOST_LOG(warning) << "Touch pointer ID not found! Did the client miss a touch down event?"sv;
        return;
      }

      contact_index = found->second;
      if (*event == lvg::touch_event::up || *event == lvg::touch_event::cancel) {
        slot.pointer_id_map.erase(found);
        slot.contacts[contact_index] = false;
      }
    }

    lvg::touch_state_request request {};
    initialize_header(request.header, sizeof(request));
    request.controller_id = static_cast<std::uint32_t>(touch.id.globalIndex);
    request.contact_index = contact_index;
    request.event_type = static_cast<std::uint8_t>(*event);
    request.x = vhf::touch_coordinate(touch.x);
    request.y = vhf::touch_coordinate(touch.y);
    request.pressure = vhf::touch_coordinate(touch.pressure);

    if (const auto status = client_.submit_touch_state(request); status != ERROR_SUCCESS) {
      BOOST_LOG(debug) << "Couldn't send a gamepad touch event to libvirtualgamepad ["sv << status << ']';
    }
  }

  void vhf_gamepad_t::update_motion(const gamepad_motion_t &motion) {
    if (motion.motionType != LI_MOTION_TYPE_ACCEL && motion.motionType != LI_MOTION_TYPE_GYRO) {
      return;
    }
    if (motion.id.globalIndex < 0 || motion.id.globalIndex >= static_cast<int>(MAX_GAMEPADS)) {
      return;
    }

    std::lock_guard lock {mutex_};
    auto &slot = slots_[motion.id.globalIndex];
    if (!slot.created || !vhf::folds_playstation_state(slot.profile)) {
      return;
    }

    lvg::motion_state_request request {};
    initialize_header(request.header, sizeof(request));
    request.controller_id = static_cast<std::uint32_t>(motion.id.globalIndex);
    request.motion_type = static_cast<std::uint8_t>(motion.motionType == LI_MOTION_TYPE_ACCEL ? lvg::motion_kind::accelerometer : lvg::motion_kind::gyroscope);
    request.x_milli = static_cast<std::int32_t>(std::lround(motion.x * 1000.0f));
    request.y_milli = static_cast<std::int32_t>(std::lround(motion.y * 1000.0f));
    request.z_milli = static_cast<std::int32_t>(std::lround(motion.z * 1000.0f));

    if (const auto status = client_.submit_motion_state(request); status != ERROR_SUCCESS) {
      BOOST_LOG(debug) << "Couldn't send gamepad motion to libvirtualgamepad ["sv << status << ']';
    }
  }

  void vhf_gamepad_t::update_battery(const gamepad_battery_t &battery) {
    if (battery.id.globalIndex < 0 || battery.id.globalIndex >= static_cast<int>(MAX_GAMEPADS)) {
      return;
    }

    std::lock_guard lock {mutex_};
    auto &slot = slots_[battery.id.globalIndex];
    if (!slot.created || !vhf::folds_playstation_state(slot.profile)) {
      return;
    }

    lvg::battery_state_request request {};
    initialize_header(request.header, sizeof(request));
    request.controller_id = static_cast<std::uint32_t>(battery.id.globalIndex);
    request.percent = battery.percentage == LI_BATTERY_PERCENTAGE_UNKNOWN ? 0 : battery.percentage;
    request.flags = static_cast<std::uint8_t>(vhf::battery_state(battery.state));

    if (const auto status = client_.submit_battery_state(request); status != ERROR_SUCCESS) {
      BOOST_LOG(debug) << "Couldn't send gamepad battery state to libvirtualgamepad ["sv << status << ']';
    }
  }

  void vhf_gamepad_t::handle_feedback(int nr, const lvg::feedback_event &event) {
    auto &slot = slots_[nr];
    if (!slot.feedback_queue) {
      return;
    }

    const auto raise_rumble = [&slot](std::uint16_t low, std::uint16_t high) {
      slot.feedback_queue->raise(gamepad_feedback_msg_t::make_rumble(slot.client_relative_index, low, high));
    };

    switch (event.type) {
      case lvg::feedback_type::xbox_rumble: {
        lvg::xbox_rumble_feedback payload {};
        std::memcpy(&payload, event.payload, sizeof(payload));
        raise_rumble(payload.low_frequency, payload.high_frequency);
        if (payload.left_trigger || payload.right_trigger) {
          slot.feedback_queue->raise(gamepad_feedback_msg_t::make_rumble_triggers(slot.client_relative_index, payload.left_trigger, payload.right_trigger));
        }
        break;
      }
      case lvg::feedback_type::generic_rumble:
      case lvg::feedback_type::generic_rumble_rgb: {
        lvg::generic_rumble_rgb_feedback payload {};
        std::memcpy(&payload, event.payload, sizeof(payload));
        raise_rumble(payload.low_frequency, payload.high_frequency);
        if (event.type == lvg::feedback_type::generic_rumble_rgb) {
          slot.feedback_queue->raise(gamepad_feedback_msg_t::make_rgb_led(slot.client_relative_index, payload.red, payload.green, payload.blue));
        }
        break;
      }
      case lvg::feedback_type::playstation_output: {
        lvg::playstation_output_feedback payload {};
        std::memcpy(&payload, event.payload, sizeof(payload));
        raise_rumble(payload.low_frequency, payload.high_frequency);

        if (payload.valid & lvg::ps_output_lightbar_valid) {
          slot.feedback_queue->raise(gamepad_feedback_msg_t::make_rgb_led(slot.client_relative_index, payload.red, payload.green, payload.blue));
        }

        if (payload.valid & lvg::ps_output_triggers_valid) {
          std::uint8_t event_flags = 0;
          std::array<std::uint8_t, 10> left {};
          std::array<std::uint8_t, 10> right {};
          std::memcpy(left.data(), payload.left_trigger.parameters, left.size());
          std::memcpy(right.data(), payload.right_trigger.parameters, right.size());

          if (payload.left_trigger.mode != static_cast<std::uint8_t>(lvg::trigger_effect_mode::off)) {
            event_flags |= 0x08;  // DS_EFFECT_LEFT_TRIGGER
          }
          if (payload.right_trigger.mode != static_cast<std::uint8_t>(lvg::trigger_effect_mode::off)) {
            event_flags |= 0x04;  // DS_EFFECT_RIGHT_TRIGGER
          }

          slot.feedback_queue->raise(gamepad_feedback_msg_t::make_adaptive_triggers(
            slot.client_relative_index,
            event_flags,
            payload.left_trigger.mode,
            payload.right_trigger.mode,
            left,
            right
          ));
        }
        break;
      }
      default:
        break;
    }
  }

  void vhf_gamepad_t::feedback_loop() {
    while (!stop_polling_) {
      {
        std::lock_guard lock {mutex_};
        if (!client_.connected()) {
          return;
        }

        for (int nr = 0; nr < static_cast<int>(slots_.size()); ++nr) {
          if (!slots_[nr].created) {
            continue;
          }

          lvg::feedback_event event {};
          while (client_.poll_feedback(static_cast<std::uint32_t>(nr), &event) == ERROR_SUCCESS) {
            handle_feedback(nr, event);
          }
        }
      }

      std::this_thread::sleep_for(4ms);
    }
  }

  vhf_gamepad_t::~vhf_gamepad_t() {
    stop_polling_ = true;
    if (feedback_thread_.joinable()) {
      feedback_thread_.join();
    }

    std::lock_guard lock {mutex_};
    for (int nr = 0; nr < static_cast<int>(slots_.size()); ++nr) {
      if (!slots_[nr].created) {
        continue;
      }

      const auto status = client_.destroy_controller(static_cast<std::uint32_t>(nr));
      if (status != ERROR_SUCCESS) {
        BOOST_LOG(debug) << "Couldn't remove the virtual gamepad during shutdown ["sv << status << ']';
      }
      slots_[nr] = {};
    }

    client_.close();
  }
}  // namespace platf
