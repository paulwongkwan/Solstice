/**
 * @file src/platform/windows/hidmaestro.cpp
 * @brief HIDMaestro backed Windows virtual gamepad backend.
 */

// platform includes
#include <Windows.h>

// standard includes
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <thread>
#include <utility>

// local includes
#include "hidmaestro.h"
#include "src/logging.h"
#include "src/platform/common.h"

namespace platf {
  using namespace std::literals;

  namespace hidmaestro {
    std::optional<profile_kind> profile_from_name(std::string_view name) {
      if (name == "x360"sv) {
        return profile_kind::xbox_360;
      }
      if (name == "xbox_one"sv) {
        return profile_kind::xbox_one;
      }
      if (name == "xbox_series"sv) {
        return profile_kind::xbox_series;
      }
      if (name == "ds4"sv || name == "dualshock4"sv) {
        return profile_kind::dualshock_4;
      }
      if (name == "dualsense"sv) {
        return profile_kind::dualsense;
      }
      if (name == "switch_pro"sv) {
        return profile_kind::switch_pro;
      }
      if (name == "steam_controller"sv) {
        return profile_kind::steam_controller;
      }
      return std::nullopt;
    }

    std::string_view name_from_profile(profile_kind profile) {
      switch (profile) {
        case profile_kind::xbox_360:
          return "x360"sv;
        case profile_kind::xbox_one:
          return "xbox_one"sv;
        case profile_kind::xbox_series:
          return "xbox_series"sv;
        case profile_kind::dualshock_4:
          return "ds4"sv;
        case profile_kind::dualsense:
          return "dualsense"sv;
        case profile_kind::switch_pro:
          return "switch_pro"sv;
        case profile_kind::steam_controller:
          return "steam_controller"sv;
      }
      return {};
    }

    bool folds_playstation_state(profile_kind profile) {
      return profile == profile_kind::dualshock_4 || profile == profile_kind::dualsense ||
             profile == profile_kind::switch_pro || profile == profile_kind::steam_controller;
    }

    std::uint8_t battery_flags(std::uint8_t state) {
      switch (state) {
        case LI_BATTERY_STATE_DISCHARGING:
          return 2;
        case LI_BATTERY_STATE_CHARGING:
          return 3;
        case LI_BATTERY_STATE_FULL:
          return 4;
        case LI_BATTERY_STATE_NOT_CHARGING:
          return 5;
        case LI_BATTERY_STATE_NOT_PRESENT:
          return 1;
        default:
          return 0;
      }
    }
  }  // namespace hidmaestro

  namespace {
    /**
     * @brief Message types understood by the bridge process.
     */
    enum class bridge_message : std::int32_t {
      ack = 0x80,
      ready = 0x81,
      rumble = 0x82,
      led = 0x83,
      log = 0x84,
      hello = 1,
      create = 2,
      destroy = 3,
      input = 4,
      touch = 5,
      motion = 6,
      battery = 7,
      shutdown = 8,
    };

    /**
     * @brief Read exactly a number of bytes from a handle.
     *
     * @param handle Handle to read from.
     * @param data Destination buffer.
     * @param size Bytes to read.
     * @return True when every byte was read.
     */
    bool read_exact(HANDLE handle, void *data, DWORD size) {
      auto *bytes = static_cast<std::uint8_t *>(data);
      DWORD remaining = size;
      while (remaining > 0) {
        DWORD read = 0;
        if (!ReadFile(handle, bytes, remaining, &read, nullptr) || read == 0) {
          return false;
        }
        bytes += read;
        remaining -= read;
      }
      return true;
    }

    /**
     * @brief Write exactly a number of bytes to a handle.
     *
     * @param handle Handle to write to.
     * @param data Source buffer.
     * @param size Bytes to write.
     * @return True when every byte was written.
     */
    bool write_exact(HANDLE handle, const void *data, DWORD size) {
      const auto *bytes = static_cast<const std::uint8_t *>(data);
      DWORD remaining = size;
      while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(handle, bytes, remaining, &written, nullptr) || written == 0) {
          return false;
        }
        bytes += written;
        remaining -= written;
      }
      return true;
    }

    /**
     * @brief Append a little-endian value to a message payload.
     *
     * @tparam T Value type.
     * @param payload Payload buffer.
     * @param value Value to append.
     */
    template<typename T>
    void append(std::vector<std::uint8_t> &payload, T value) {
      const auto offset = payload.size();
      payload.resize(offset + sizeof(T));
      std::memcpy(payload.data() + offset, &value, sizeof(T));
    }

    /**
     * @brief Read a little-endian value from a message payload.
     *
     * @tparam T Value type.
     * @param payload Payload buffer.
     * @param offset Byte offset.
     * @return Value read, or a default value when the payload is too short.
     */
    template<typename T>
    T read_value(const std::vector<std::uint8_t> &payload, std::size_t offset) {
      T value {};
      if (payload.size() >= offset + sizeof(T)) {
        std::memcpy(&value, payload.data() + offset, sizeof(T));
      }
      return value;
    }
  }  // namespace

  int hidmaestro_t::init() {
    slots_.clear();
    slots_.resize(MAX_GAMEPADS);

    const auto executable = std::filesystem::path(SOLSTICE_ASSETS_DIR).parent_path() / "drivers" / "hidmaestro" / "HIDMaestroBridge.exe";
    if (!std::filesystem::exists(executable)) {
      BOOST_LOG(debug) << "HIDMaestro bridge is not installed ["sv << executable.string() << ']';
      available_ = false;
      status_message_ = "HIDMaestro bridge is not installed";
      return -1;
    }

    const auto pipe_name = std::format("\\\\.\\pipe\\solstice-hidmaestro-{}-{}", GetCurrentProcessId(), reinterpret_cast<std::uintptr_t>(this));
    const std::wstring command_line = std::format(L"\"{}\" --pipe solstice-hidmaestro-{}-{}", executable.wstring(), GetCurrentProcessId(), reinterpret_cast<std::uintptr_t>(this));

    STARTUPINFOW startup_info {};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info {};
    std::wstring mutable_command_line = command_line;
    if (!CreateProcessW(
          nullptr,
          mutable_command_line.data(),
          nullptr,
          nullptr,
          FALSE,
          CREATE_NO_WINDOW,
          nullptr,
          nullptr,
          &startup_info,
          &process_info)) {
      BOOST_LOG(warning) << "Couldn't start the HIDMaestro bridge ["sv << GetLastError() << ']';
      available_ = false;
      status_message_ = "HIDMaestro bridge did not start";
      return -1;
    }

    process_ = process_info.hProcess;
    CloseHandle(process_info.hThread);

    // The bridge creates the pipe before it connects, so a short retry window is enough.
    for (int attempt = 0; attempt < 200 && pipe_ == INVALID_HANDLE_VALUE; ++attempt) {
      pipe_ = CreateFileW(
        std::format(L"\\\\.\\pipe\\solstice-hidmaestro-{}-{}", GetCurrentProcessId(), reinterpret_cast<std::uintptr_t>(this)).c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
      if (pipe_ == INVALID_HANDLE_VALUE) {
        std::this_thread::sleep_for(50ms);
      }
    }

    if (pipe_ == INVALID_HANDLE_VALUE) {
      BOOST_LOG(warning) << "Couldn't connect to the HIDMaestro bridge pipe"sv;
      available_ = false;
      status_message_ = "HIDMaestro bridge did not answer";
      shutdown();
      return -1;
    }

    std::int32_t type = 0;
    std::int32_t length = 0;
    if (!read_exact(pipe_, &type, sizeof(type)) || !read_exact(pipe_, &length, sizeof(length)) ||
        type != static_cast<std::int32_t>(bridge_message::ready) || length < 12 || length > 4096) {
      BOOST_LOG(warning) << "The HIDMaestro bridge sent an unexpected handshake"sv;
      available_ = false;
      status_message_ = "HIDMaestro bridge handshake failed";
      shutdown();
      return -1;
    }

    std::vector<std::uint8_t> payload(static_cast<std::size_t>(length));
    if (!read_exact(pipe_, payload.data(), static_cast<DWORD>(payload.size()))) {
      available_ = false;
      status_message_ = "HIDMaestro bridge handshake failed";
      shutdown();
      return -1;
    }

    const auto status = read_value<std::int32_t>(payload, 0);
    const auto message_length = read_value<std::int32_t>(payload, 8);
    status_message_.assign(
      reinterpret_cast<const char *>(payload.data() + 12),
      std::min<std::size_t>(static_cast<std::size_t>(std::max(message_length, 0)), payload.size() - 12));

    available_ = status == 0;
    if (!available_) {
      BOOST_LOG(warning) << "HIDMaestro is unavailable: "sv << status_message_;
      shutdown();
      return -1;
    }

    BOOST_LOG(info) << "HIDMaestro bridge is ready"sv;
    reader_thread_ = std::thread {&hidmaestro_t::reader_loop, this};
    return 0;
  }

  bool hidmaestro_t::available() const noexcept {
    return available_;
  }

  std::string hidmaestro_t::status_message() const {
    return status_message_;
  }

  bool hidmaestro_t::write_message(std::int32_t type, const std::vector<std::uint8_t> &payload) {
    if (pipe_ == INVALID_HANDLE_VALUE) {
      return false;
    }

    const auto length = static_cast<std::int32_t>(payload.size());
    if (!write_exact(pipe_, &type, sizeof(type)) || !write_exact(pipe_, &length, sizeof(length))) {
      return false;
    }
    return payload.empty() || write_exact(pipe_, payload.data(), static_cast<DWORD>(payload.size()));
  }

  int hidmaestro_t::alloc_gamepad(const gamepad_id_t &id, feedback_queue_t feedback_queue, hidmaestro::profile_kind profile) {
    if (id.globalIndex < 0 || id.globalIndex >= static_cast<int>(MAX_GAMEPADS) || !available_) {
      return -1;
    }

    std::lock_guard lock {mutex_};
    {
      std::lock_guard ack_lock {ack_mutex_};
      pending_ack_slot_ = id.globalIndex;
      pending_ack_status_ = std::numeric_limits<int>::min();
    }

    std::vector<std::uint8_t> payload;
    append<std::int32_t>(payload, id.globalIndex);
    append<std::int32_t>(payload, static_cast<std::int32_t>(profile));
    if (!write_message(static_cast<std::int32_t>(bridge_message::create), payload)) {
      BOOST_LOG(warning) << "Couldn't ask HIDMaestro for a virtual gamepad"sv;
      std::lock_guard ack_lock {ack_mutex_};
      pending_ack_slot_ = -1;
      return -1;
    }

    {
      std::unique_lock ack_lock {ack_mutex_};
      const auto answered = ack_condition_.wait_for(ack_lock, 5s, [this] {
        return pending_ack_status_ != std::numeric_limits<int>::min();
      });
      const auto status = pending_ack_status_;
      pending_ack_slot_ = -1;
      if (!answered || status != 0) {
        BOOST_LOG(warning) << "HIDMaestro couldn't create a "sv << hidmaestro::name_from_profile(profile) << " controller ["sv << status << ']';
        return -1;
      }
    }

    auto &slot = slots_[id.globalIndex];
    slot.created = true;
    slot.profile = profile;
    slot.client_relative_index = id.clientRelativeIndex;
    slot.feedback_queue = std::move(feedback_queue);
    slot.pointer_id_map.clear();

    if (hidmaestro::folds_playstation_state(profile) && slot.feedback_queue) {
      slot.feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(slot.client_relative_index, LI_MOTION_TYPE_ACCEL, 100));
      slot.feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(slot.client_relative_index, LI_MOTION_TYPE_GYRO, 100));
    }

    BOOST_LOG(info) << "Gamepad "sv << id.globalIndex << " is a "sv << hidmaestro::name_from_profile(profile) << " controller through HIDMaestro"sv;
    return 0;
  }

  int hidmaestro_t::rebind_gamepad(const gamepad_id_t &id, feedback_queue_t feedback_queue) {
    std::lock_guard lock {mutex_};
    if (id.globalIndex < 0 || id.globalIndex >= static_cast<int>(MAX_GAMEPADS) || !slots_[id.globalIndex].created) {
      return -1;
    }

    auto &slot = slots_[id.globalIndex];
    slot.client_relative_index = id.clientRelativeIndex;
    slot.feedback_queue = std::move(feedback_queue);
    return 0;
  }

  void hidmaestro_t::free_gamepad(int nr) {
    if (nr < 0 || nr >= static_cast<int>(MAX_GAMEPADS)) {
      return;
    }

    std::lock_guard lock {mutex_};
    auto &slot = slots_[nr];
    if (!slot.created) {
      return;
    }

    std::vector<std::uint8_t> payload;
    append<std::int32_t>(payload, nr);
    write_message(static_cast<std::int32_t>(bridge_message::destroy), payload);
    slot = {};
  }

  bool hidmaestro_t::attached(int nr) const {
    std::lock_guard lock {mutex_};
    return nr >= 0 && nr < static_cast<int>(MAX_GAMEPADS) && slots_[nr].created;
  }

  void hidmaestro_t::update_gamepad(int nr, const gamepad_state_t &state) {
    if (nr < 0 || nr >= static_cast<int>(MAX_GAMEPADS)) {
      return;
    }

    std::lock_guard lock {mutex_};
    if (!slots_[nr].created) {
      return;
    }

    std::vector<std::uint8_t> payload;
    append<std::int32_t>(payload, nr);
    append<std::uint32_t>(payload, state.buttonFlags);
    append<std::int16_t>(payload, state.lsX);
    append<std::int16_t>(payload, state.lsY);
    append<std::int16_t>(payload, state.rsX);
    append<std::int16_t>(payload, state.rsY);
    append<std::uint8_t>(payload, state.lt);
    append<std::uint8_t>(payload, state.rt);
    write_message(static_cast<std::int32_t>(bridge_message::input), payload);
  }

  void hidmaestro_t::update_touch(const gamepad_touch_t &touch) {
    if (touch.id.globalIndex < 0 || touch.id.globalIndex >= static_cast<int>(MAX_GAMEPADS)) {
      return;
    }

    std::lock_guard lock {mutex_};
    auto &slot = slots_[touch.id.globalIndex];
    if (!slot.created || !hidmaestro::folds_playstation_state(slot.profile)) {
      return;
    }

    std::uint8_t contact = 0;
    if (touch.eventType == LI_TOUCH_EVENT_CANCEL_ALL) {
      slot.pointer_id_map.clear();
    } else if (touch.eventType == LI_TOUCH_EVENT_DOWN || touch.eventType == LI_TOUCH_EVENT_HOVER) {
      const auto found = slot.pointer_id_map.find(touch.pointerId);
      if (found != slot.pointer_id_map.end()) {
        contact = found->second;
      } else {
        contact = slot.pointer_id_map.size() >= 2 ? 0 : static_cast<std::uint8_t>(slot.pointer_id_map.size());
        slot.pointer_id_map[touch.pointerId] = contact;
      }
    } else {
      const auto found = slot.pointer_id_map.find(touch.pointerId);
      if (found == slot.pointer_id_map.end()) {
        return;
      }
      contact = found->second;
      if (touch.eventType == LI_TOUCH_EVENT_UP || touch.eventType == LI_TOUCH_EVENT_CANCEL) {
        slot.pointer_id_map.erase(found);
      }
    }

    const auto coordinate = [](float value) {
      return static_cast<std::uint16_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 65535.0f));
    };

    std::vector<std::uint8_t> payload;
    append<std::int32_t>(payload, touch.id.globalIndex);
    append<std::uint8_t>(payload, contact);
    append<std::uint8_t>(payload, touch.eventType);
    append<std::uint16_t>(payload, coordinate(touch.x));
    append<std::uint16_t>(payload, coordinate(touch.y));
    append<std::uint16_t>(payload, coordinate(touch.pressure));
    write_message(static_cast<std::int32_t>(bridge_message::touch), payload);
  }

  void hidmaestro_t::update_motion(const gamepad_motion_t &motion) {
    if (motion.id.globalIndex < 0 || motion.id.globalIndex >= static_cast<int>(MAX_GAMEPADS)) {
      return;
    }

    std::lock_guard lock {mutex_};
    const auto &slot = slots_[motion.id.globalIndex];
    if (!slot.created || !hidmaestro::folds_playstation_state(slot.profile)) {
      return;
    }

    std::vector<std::uint8_t> payload;
    append<std::int32_t>(payload, motion.id.globalIndex);
    append<std::uint8_t>(payload, motion.motionType);
    append<std::uint8_t>(payload, 0);
    append<std::uint8_t>(payload, 0);
    append<std::uint8_t>(payload, 0);
    append<std::int32_t>(payload, static_cast<std::int32_t>(std::lround(motion.x * 1000.0f)));
    append<std::int32_t>(payload, static_cast<std::int32_t>(std::lround(motion.y * 1000.0f)));
    append<std::int32_t>(payload, static_cast<std::int32_t>(std::lround(motion.z * 1000.0f)));
    write_message(static_cast<std::int32_t>(bridge_message::motion), payload);
  }

  void hidmaestro_t::update_battery(const gamepad_battery_t &battery) {
    if (battery.id.globalIndex < 0 || battery.id.globalIndex >= static_cast<int>(MAX_GAMEPADS)) {
      return;
    }

    std::lock_guard lock {mutex_};
    const auto &slot = slots_[battery.id.globalIndex];
    if (!slot.created || !hidmaestro::folds_playstation_state(slot.profile)) {
      return;
    }

    std::vector<std::uint8_t> payload;
    append<std::int32_t>(payload, battery.id.globalIndex);
    append<std::uint8_t>(payload, battery.percentage == LI_BATTERY_PERCENTAGE_UNKNOWN ? 0 : battery.percentage);
    append<std::uint8_t>(payload, hidmaestro::battery_flags(battery.state));
    write_message(static_cast<std::int32_t>(bridge_message::battery), payload);
  }

  void hidmaestro_t::handle_rumble(const std::vector<std::uint8_t> &payload) {
    const auto slot_index = read_value<std::int32_t>(payload, 0);
    if (slot_index < 0 || slot_index >= static_cast<int>(MAX_GAMEPADS)) {
      return;
    }

    std::lock_guard lock {mutex_};
    auto &slot = slots_[slot_index];
    if (!slot.feedback_queue) {
      return;
    }

    const auto low = read_value<std::uint16_t>(payload, 8);
    const auto high = read_value<std::uint16_t>(payload, 10);
    const auto left_trigger = read_value<std::uint16_t>(payload, 12);
    const auto right_trigger = read_value<std::uint16_t>(payload, 14);

    slot.feedback_queue->raise(gamepad_feedback_msg_t::make_rumble(slot.client_relative_index, low, high));
    if (left_trigger || right_trigger) {
      slot.feedback_queue->raise(gamepad_feedback_msg_t::make_rumble_triggers(slot.client_relative_index, left_trigger, right_trigger));
    }
  }

  void hidmaestro_t::handle_led(const std::vector<std::uint8_t> &payload) {
    const auto slot_index = read_value<std::int32_t>(payload, 0);
    if (slot_index < 0 || slot_index >= static_cast<int>(MAX_GAMEPADS)) {
      return;
    }

    std::lock_guard lock {mutex_};
    auto &slot = slots_[slot_index];
    if (!slot.feedback_queue) {
      return;
    }

    slot.feedback_queue->raise(gamepad_feedback_msg_t::make_rgb_led(
      slot.client_relative_index,
      read_value<std::uint8_t>(payload, 4),
      read_value<std::uint8_t>(payload, 5),
      read_value<std::uint8_t>(payload, 6)));
  }

  void hidmaestro_t::handle_ack(const std::vector<std::uint8_t> &payload) {
    const auto slot = read_value<std::int32_t>(payload, 0);
    const auto status = read_value<std::int32_t>(payload, 4);

    std::lock_guard lock {ack_mutex_};
    if (slot == pending_ack_slot_) {
      pending_ack_status_ = status;
      ack_condition_.notify_all();
    }
  }

  void hidmaestro_t::reader_loop() {
    while (pipe_ != INVALID_HANDLE_VALUE) {
      std::int32_t type = 0;
      std::int32_t length = 0;
      if (!read_exact(pipe_, &type, sizeof(type)) || !read_exact(pipe_, &length, sizeof(length)) ||
          length < 0 || length > 4096) {
        return;
      }

      std::vector<std::uint8_t> payload(static_cast<std::size_t>(length));
      if (length > 0 && !read_exact(pipe_, payload.data(), static_cast<DWORD>(payload.size()))) {
        return;
      }

      if (type == static_cast<std::int32_t>(bridge_message::ack)) {
        handle_ack(payload);
      } else if (type == static_cast<std::int32_t>(bridge_message::rumble)) {
        handle_rumble(payload);
      } else if (type == static_cast<std::int32_t>(bridge_message::led)) {
        handle_led(payload);
      } else if (type == static_cast<std::int32_t>(bridge_message::log) && !payload.empty()) {
        BOOST_LOG(debug) << "HIDMaestro: "sv << std::string_view {reinterpret_cast<const char *>(payload.data()), payload.size()};
      }
    }
  }

  void hidmaestro_t::shutdown() noexcept {
    if (pipe_ != INVALID_HANDLE_VALUE) {
      std::vector<std::uint8_t> payload;
      const std::int32_t type = static_cast<std::int32_t>(bridge_message::shutdown);
      const std::int32_t length = 0;
      write_exact(pipe_, &type, sizeof(type));
      write_exact(pipe_, &length, sizeof(length));
      CloseHandle(pipe_);
      pipe_ = INVALID_HANDLE_VALUE;
    }

    if (process_) {
      if (WaitForSingleObject(process_, 2000) == WAIT_TIMEOUT) {
        TerminateProcess(process_, 0);
      }
      CloseHandle(process_);
      process_ = nullptr;
    }
  }

  hidmaestro_t::~hidmaestro_t() {
    if (reader_thread_.joinable()) {
      // Closing the pipe unblocks the reader.
      if (pipe_ != INVALID_HANDLE_VALUE) {
        CancelSynchronousIo(reinterpret_cast<HANDLE>(reader_thread_.native_handle()));
      }
      reader_thread_.join();
    }
    shutdown();
  }
}  // namespace platf
