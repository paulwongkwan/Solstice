/**
 * @file tests/unit/platform/windows/test_input.cpp
 * @brief Tests for the PadBus state helpers used by the Windows input backend.
 */

// test includes
#include "../../../tests_common.h"

#ifdef _WIN32
  // standard includes
  #include <array>
  #include <cstdint>
  #include <map>
  #include <tuple>

  // local includes
  #include <src/platform/windows/input.h>

using namespace platf;

TEST(WindowsStickTest, NormalizesTheFullAxisRange) {
  EXPECT_FLOAT_EQ(platf::gamepad::normalize_stick(0), 0.0f);
  EXPECT_FLOAT_EQ(platf::gamepad::normalize_stick(32767), 1.0f);
  EXPECT_FLOAT_EQ(platf::gamepad::normalize_stick(-32767), -1.0f);
  EXPECT_FLOAT_EQ(platf::gamepad::normalize_stick(-32768), -1.0f);
  EXPECT_FLOAT_EQ(platf::gamepad::normalize_stick(16384), 16384.0f / 32767.0f);
}

TEST(WindowsTriggerTest, NormalizesTheFullTriggerRange) {
  EXPECT_FLOAT_EQ(platf::gamepad::normalize_trigger(0), 0.0f);
  EXPECT_FLOAT_EQ(platf::gamepad::normalize_trigger(255), 1.0f);
  EXPECT_FLOAT_EQ(platf::gamepad::normalize_trigger(51), 51.0f / 255.0f);
}

TEST(WindowsButtonTest, MapsEverySharedClientButton) {
  const auto buttons = platf::gamepad::buttons(
    DPAD_UP | DPAD_DOWN | DPAD_LEFT | DPAD_RIGHT | START | BACK | LEFT_STICK | RIGHT_STICK | LEFT_BUTTON | RIGHT_BUTTON | A | B | X | Y,
    false,
    false
  );

  EXPECT_TRUE(buttons.test(padbus::GamepadButton::dpad_up));
  EXPECT_TRUE(buttons.test(padbus::GamepadButton::dpad_down));
  EXPECT_TRUE(buttons.test(padbus::GamepadButton::dpad_left));
  EXPECT_TRUE(buttons.test(padbus::GamepadButton::dpad_right));
  EXPECT_TRUE(buttons.test(padbus::GamepadButton::start));
  EXPECT_TRUE(buttons.test(padbus::GamepadButton::back));
  EXPECT_TRUE(buttons.test(padbus::GamepadButton::left_stick));
  EXPECT_TRUE(buttons.test(padbus::GamepadButton::right_stick));
  EXPECT_TRUE(buttons.test(padbus::GamepadButton::left_shoulder));
  EXPECT_TRUE(buttons.test(padbus::GamepadButton::right_shoulder));
  EXPECT_TRUE(buttons.test(padbus::GamepadButton::a));
  EXPECT_TRUE(buttons.test(padbus::GamepadButton::b));
  EXPECT_TRUE(buttons.test(padbus::GamepadButton::x));
  EXPECT_TRUE(buttons.test(padbus::GamepadButton::y));
  EXPECT_FALSE(buttons.test(padbus::GamepadButton::guide));
  EXPECT_FALSE(buttons.test(padbus::GamepadButton::touchpad));
}

TEST(WindowsButtonTest, RoutesHomeAndMiscButtonsPerProfile) {
  const auto x360 = platf::gamepad::buttons(HOME | MISC_BUTTON, false, false);
  EXPECT_TRUE(x360.test(padbus::GamepadButton::guide));
  EXPECT_FALSE(x360.test(padbus::GamepadButton::touchpad));

  const auto ds4 = platf::gamepad::buttons(HOME, true, false);
  EXPECT_TRUE(ds4.test(padbus::GamepadButton::guide));
  EXPECT_FALSE(ds4.test(padbus::GamepadButton::touchpad));

  EXPECT_TRUE(platf::gamepad::buttons(TOUCHPAD_BUTTON, true, false).test(padbus::GamepadButton::touchpad));
  EXPECT_TRUE(platf::gamepad::buttons(MISC_BUTTON, true, false).test(padbus::GamepadButton::touchpad));
  EXPECT_FALSE(platf::gamepad::buttons(MISC_BUTTON, true, false).test(padbus::GamepadButton::guide));
}

TEST(WindowsButtonTest, MapsBackToTheTouchpadOnlyWhenRequestedForTheDualShock4) {
  EXPECT_TRUE(platf::gamepad::buttons(BACK, true, true).test(padbus::GamepadButton::touchpad));
  EXPECT_TRUE(platf::gamepad::buttons(BACK, true, true).test(padbus::GamepadButton::back));
  EXPECT_FALSE(platf::gamepad::buttons(BACK, true, false).test(padbus::GamepadButton::touchpad));
  EXPECT_FALSE(platf::gamepad::buttons(BACK, false, true).test(padbus::GamepadButton::touchpad));
}

/**
 * @brief Driver-free PadBus touchpad contact and pointer state.
 */
class WindowsTouchTest: public testing::Test {
private:
  std::array<padbus::GamepadTouchContact, 2> contacts_ {};  ///< Contacts submitted with the next report.
  std::map<std::uint32_t, std::uint8_t> pointers_;  ///< Allocated client pointer IDs.

protected:
  /**
   * @brief Read the touchpad contacts.
   *
   * @return Const reference to the contact slots.
   */
  const std::array<padbus::GamepadTouchContact, 2> &contacts() const {
    return contacts_;
  }

  /**
   * @brief Read the allocated client pointer IDs.
   *
   * @return Const reference to the pointer-to-slot mapping.
   */
  const std::map<std::uint32_t, std::uint8_t> &pointers() const {
    return pointers_;
  }

  /**
   * @brief Apply a client touch event without submitting it to a driver.
   *
   * @param type Client event type.
   * @param id Client pointer ID.
   * @param x Normalized horizontal coordinate.
   * @param y Normalized vertical coordinate.
   * @return Whether the resulting contacts should be submitted.
   */
  bool touch(std::uint8_t type, std::uint32_t id, float x = 0.0f, float y = 0.0f) {
    return platf::gamepad::update_touch(contacts_, pointers_, {{0, 0}, type, id, x, y, 1.0f});
  }
};

TEST_F(WindowsTouchTest, AllocatesBothSlotsAndPacksCoordinatesInContactOrder) {
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 10, 1.0f, 1.0f));
  EXPECT_EQ(pointers().at(10), 0);
  EXPECT_EQ(contacts().at(0).id, 0);
  EXPECT_TRUE(contacts().at(0).active);
  EXPECT_FLOAT_EQ(contacts().at(0).x, 1.0f);
  EXPECT_FLOAT_EQ(contacts().at(0).y, 1.0f);

  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 20, 0.5f, 0.25f));
  EXPECT_EQ(pointers().at(20), 1);
  EXPECT_EQ(contacts().at(1).id, 1);
  EXPECT_TRUE(contacts().at(1).active);
  EXPECT_FLOAT_EQ(contacts().at(1).x, 0.5f);
  EXPECT_FLOAT_EQ(contacts().at(1).y, 0.25f);

  EXPECT_FALSE(touch(LI_TOUCH_EVENT_DOWN, 30));
  EXPECT_EQ(pointers().size(), 2U);
}

TEST_F(WindowsTouchTest, MovesReleasesCancelsAndReusesTheCorrectSlot) {
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 10));
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 20));
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_MOVE, 20, 1.0f, 1.0f));
  EXPECT_FLOAT_EQ(contacts().at(1).x, 1.0f);
  EXPECT_FLOAT_EQ(contacts().at(0).x, 0.0f);

  ASSERT_TRUE(touch(LI_TOUCH_EVENT_UP, 10));
  EXPECT_FALSE(contacts().at(0).active);
  EXPECT_FALSE(pointers().contains(10));

  ASSERT_TRUE(touch(LI_TOUCH_EVENT_CANCEL, 20));
  EXPECT_FALSE(contacts().at(1).active);
  EXPECT_TRUE(pointers().empty());

  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 30));
  EXPECT_EQ(pointers().at(30), 0);
  EXPECT_TRUE(contacts().at(0).active);
}

TEST_F(WindowsTouchTest, CancelAllReleasesBothSlotsAndPreservesCoordinates) {
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 10, 1.0f, 1.0f));
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 20, 0.5f, 0.5f));
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_CANCEL_ALL, 0));

  EXPECT_FALSE(contacts().at(0).active);
  EXPECT_FALSE(contacts().at(1).active);
  EXPECT_FLOAT_EQ(contacts().at(0).x, 1.0f);
  EXPECT_FLOAT_EQ(contacts().at(1).x, 0.5f);
  EXPECT_TRUE(pointers().empty());
}

TEST_F(WindowsTouchTest, RejectsUnknownPointersAndUnsupportedEventsWithoutUpdatingTheContacts) {
  EXPECT_FALSE(touch(LI_TOUCH_EVENT_MOVE, 10));
  EXPECT_FALSE(touch(LI_TOUCH_EVENT_UP, 10));
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 10));
  EXPECT_FALSE(touch(0xFF, 10));
  EXPECT_TRUE(contacts().at(0).active);
  EXPECT_EQ(pointers().at(10), 0);
}

TEST_F(WindowsTouchTest, ClampsOutOfRangeCoordinates) {
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 10, -1.0f, 2.0f));
  EXPECT_FLOAT_EQ(contacts().at(0).x, 0.0f);
  EXPECT_FLOAT_EQ(contacts().at(0).y, 1.0f);
}

/**
 * @brief Client battery state, percentage, expected presence, percentage, and charge state.
 */
using BatteryParam = std::tuple<std::uint8_t, std::uint8_t, bool, std::uint8_t, bool>;

/**
 * @brief Battery mapping cases including special states and unknown percentage.
 */
struct WindowsBatteryTest: testing::TestWithParam<BatteryParam> {};

TEST_P(WindowsBatteryTest, MapsTheClientStateToThePadBusBatteryModel) {
  const auto &[state, percentage, present, expected_percentage, expected_charging] = GetParam();
  const auto mapped = platf::gamepad::battery({{0, 0}, state, percentage});

  ASSERT_EQ(mapped.has_value(), present);
  if (present) {
    EXPECT_EQ(mapped->percent_remaining, expected_percentage);
    EXPECT_EQ(mapped->charging, expected_charging);
  }
}

INSTANTIATE_TEST_SUITE_P(
  BatteryStates,
  WindowsBatteryTest,
  testing::Values(
    BatteryParam {LI_BATTERY_STATE_CHARGING, LI_BATTERY_PERCENTAGE_UNKNOWN, true, 100, true},
    BatteryParam {LI_BATTERY_STATE_CHARGING, 75, true, 75, true},
    BatteryParam {LI_BATTERY_STATE_DISCHARGING, LI_BATTERY_PERCENTAGE_UNKNOWN, true, 0, false},
    BatteryParam {LI_BATTERY_STATE_DISCHARGING, 40, true, 40, false},
    BatteryParam {LI_BATTERY_STATE_FULL, 20, true, 100, true},
    BatteryParam {LI_BATTERY_STATE_NOT_PRESENT, 50, false, 0, false},
    BatteryParam {LI_BATTERY_STATE_NOT_CHARGING, 50, false, 0, false},
    BatteryParam {LI_BATTERY_STATE_UNKNOWN, LI_BATTERY_PERCENTAGE_UNKNOWN, false, 0, false},
    BatteryParam {LI_BATTERY_STATE_UNKNOWN, 60, true, 60, false},
    BatteryParam {LI_BATTERY_STATE_UNKNOWN, 200, true, 100, false}
  )
);

TEST(WindowsMotionTest, ConvertsAccelerationToTheRawDualShock4Scale) {
  const auto rest = platf::gamepad::ds4_acceleration(0.0f, 9.80665f, 0.0f);
  EXPECT_NEAR(rest.y, 8032.0f, 2.0f);
  EXPECT_NEAR(rest.x, -293.0f, 2.0f);
  EXPECT_NEAR(rest.z, -499.0f, 2.0f);

  // Values outside the report range are clamped instead of wrapping
  EXPECT_FLOAT_EQ(platf::gamepad::ds4_acceleration(1.0e6f, 0.0f, 0.0f).x, 32767.0f);
  EXPECT_FLOAT_EQ(platf::gamepad::ds4_acceleration(-1.0e6f, 0.0f, 0.0f).x, -32768.0f);
}

TEST(WindowsMotionTest, ConvertsGyroscopeSamplesToTheRawDualShock4Scale) {
  const auto rest = platf::gamepad::ds4_gyroscope(0.0f, 0.0f, 0.0f);
  EXPECT_NEAR(rest.x, 1.0f, 1.0f);
  EXPECT_NEAR(rest.y, 0.0f, 1.0f);
  EXPECT_NEAR(rest.z, 0.0f, 1.0f);

  // One degree per second is roughly 16 raw units in the DualShock 4 report
  const auto sample = platf::gamepad::ds4_gyroscope(68.0f, -68.0f, 0.0f);
  EXPECT_GT(sample.x, 1000.0f);
  EXPECT_LT(sample.y, -1000.0f);

  EXPECT_FLOAT_EQ(platf::gamepad::ds4_gyroscope(1.0e6f, 0.0f, 0.0f).x, 32767.0f);
  EXPECT_FLOAT_EQ(platf::gamepad::ds4_gyroscope(-1.0e6f, 0.0f, 0.0f).x, -32768.0f);
}
#endif
