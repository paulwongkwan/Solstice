/**
 * @file src/platform/windows/input.h
 * @brief PadBus report helpers that do not submit input to a device.
 */
#pragma once

// standard includes
#include <array>
#include <cstdint>
#include <map>
#include <optional>

// lib includes
#include <padbus/padbus.hpp>

// local includes
#include "src/platform/common.h"

namespace platf::gamepad {
  /**
   * @brief Convert a signed 16-bit client stick axis to PadBus' normalized range.
   *
   * @param value Client axis value.
   * @return Axis in the inclusive range `[-1.0, 1.0]`.
   */
  float normalize_stick(std::int16_t value);

  /**
   * @brief Convert an 8-bit client trigger value to PadBus' normalized range.
   *
   * @param value Client trigger value.
   * @return Trigger in the inclusive range `[0.0, 1.0]`.
   */
  float normalize_trigger(std::uint8_t value);

  /**
   * @brief Build the PadBus button set for a client button mask.
   *
   * @param flags Moonlight button mask for the current gamepad state.
   * @param dualshock4 Whether the emulated device is a DualShock 4.
   * @param back_as_touchpad_click Whether the back button also clicks the touchpad.
   * @return Pressed PadBus buttons.
   */
  padbus::ButtonSet buttons(std::uint32_t flags, bool dualshock4, bool back_as_touchpad_click);

  /**
   * @brief Apply a client touch event to the two PadBus touchpad contacts.
   *
   * @param contacts Touchpad contacts submitted with the next report.
   * @param pointer_id_map Mapping from client pointer IDs to the two contact slots.
   * @param touch Client touch event.
   * @return True when the contacts changed and the report should be submitted.
   */
  bool update_touch(std::array<padbus::GamepadTouchContact, 2> &contacts, std::map<std::uint32_t, std::uint8_t> &pointer_id_map, const gamepad_touch_t &touch);

  /**
   * @brief Convert a client battery event to PadBus' battery model.
   *
   * @param battery Client battery state and percentage.
   * @return Battery state, or no value when the client has nothing to report.
   */
  std::optional<padbus::GamepadBattery> battery(const gamepad_battery_t &battery);

  /**
   * @brief Convert an accelerometer sample to the raw DualShock 4 scale.
   *
   * @param x X component in m/s^2.
   * @param y Y component in m/s^2.
   * @param z Z component in m/s^2.
   * @return Raw DualShock 4 accelerometer sample.
   */
  padbus::Vector3 ds4_acceleration(float x, float y, float z);

  /**
   * @brief Convert a gyroscope sample to the raw DualShock 4 scale.
   *
   * @param x X component in deg/s.
   * @param y Y component in deg/s.
   * @param z Z component in deg/s.
   * @return Raw DualShock 4 gyroscope sample.
   */
  padbus::Vector3 ds4_gyroscope(float x, float y, float z);
}  // namespace platf::gamepad
