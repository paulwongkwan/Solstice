/**
 * @file tests/unit/platform/windows/test_vhf_gamepad.cpp
 * @brief Tests for the libvirtualgamepad protocol translation helpers.
 */

// test includes
#include "../../../tests_common.h"

#ifdef _WIN32
  // standard includes
  #include <cstdint>

  // local includes
  #include <src/platform/common.h>
  #include <src/platform/windows/vhf_gamepad.h>

using namespace platf;

TEST(WindowsVhfProfileTest, MapsEveryConfiguredNameToItsDriverProfile) {
  EXPECT_EQ(platf::vhf::profile_from_name("ds4"), lvg::profile::dualshock_4);
  EXPECT_EQ(platf::vhf::profile_from_name("dualshock4"), lvg::profile::dualshock_4);
  EXPECT_EQ(platf::vhf::profile_from_name("dualsense"), lvg::profile::dualsense);
  EXPECT_EQ(platf::vhf::profile_from_name("xbox_one"), lvg::profile::xbox_one);
  EXPECT_EQ(platf::vhf::profile_from_name("xbox_series"), lvg::profile::xbox_series);
  EXPECT_EQ(platf::vhf::profile_from_name("switch_pro"), lvg::profile::switch_pro);
  EXPECT_EQ(platf::vhf::profile_from_name("steam_controller"), lvg::profile::steam_controller);
}

TEST(WindowsVhfProfileTest, LeavesNamesTheDriverCannotCreateUnmapped) {
  EXPECT_FALSE(platf::vhf::profile_from_name("auto").has_value());
  EXPECT_FALSE(platf::vhf::profile_from_name("x360").has_value());
  EXPECT_FALSE(platf::vhf::profile_from_name("").has_value());
  EXPECT_FALSE(platf::vhf::profile_from_name("dualshock_4").has_value());
}

TEST(WindowsVhfProfileTest, RoundTripsEverySupportedProfileName) {
  for (const auto profile : {
         lvg::profile::dualshock_4,
         lvg::profile::dualsense,
         lvg::profile::xbox_one,
         lvg::profile::xbox_series,
         lvg::profile::switch_pro,
         lvg::profile::steam_controller,
       }) {
    const auto name = platf::vhf::name_from_profile(profile);
    EXPECT_FALSE(name.empty());
    EXPECT_EQ(platf::vhf::profile_from_name(name), profile);
  }
}

TEST(WindowsVhfProfileTest, KeepsXbox360AndReservedProfilesUnnamed) {
  EXPECT_TRUE(platf::vhf::name_from_profile(lvg::profile::xbox_360).empty());
  EXPECT_TRUE(platf::vhf::name_from_profile(lvg::profile::generic_hid).empty());
  EXPECT_TRUE(platf::vhf::name_from_profile(lvg::profile::generic_pid).empty());
}

TEST(WindowsVhfProfileTest, OnlyPlayStationLikeProfilesFoldTouchMotionAndBattery) {
  EXPECT_TRUE(platf::vhf::folds_playstation_state(lvg::profile::dualshock_4));
  EXPECT_TRUE(platf::vhf::folds_playstation_state(lvg::profile::dualsense));
  EXPECT_TRUE(platf::vhf::folds_playstation_state(lvg::profile::switch_pro));
  EXPECT_TRUE(platf::vhf::folds_playstation_state(lvg::profile::steam_controller));
  EXPECT_FALSE(platf::vhf::folds_playstation_state(lvg::profile::xbox_one));
  EXPECT_FALSE(platf::vhf::folds_playstation_state(lvg::profile::xbox_series));
  EXPECT_FALSE(platf::vhf::folds_playstation_state(lvg::profile::xbox_360));
}

TEST(WindowsVhfButtonTest, PreservesEveryMoonlightButton) {
  const std::uint32_t flags =
    DPAD_UP | DPAD_DOWN | DPAD_LEFT | DPAD_RIGHT | START | BACK | LEFT_STICK | RIGHT_STICK |
    LEFT_BUTTON | RIGHT_BUTTON | HOME | A | B | X | Y | PADDLE1 | PADDLE2 | PADDLE3 | PADDLE4;

  const std::uint32_t expected =
    static_cast<std::uint32_t>(lvg::button_mask::dpad_up) |
    static_cast<std::uint32_t>(lvg::button_mask::dpad_down) |
    static_cast<std::uint32_t>(lvg::button_mask::dpad_left) |
    static_cast<std::uint32_t>(lvg::button_mask::dpad_right) |
    static_cast<std::uint32_t>(lvg::button_mask::start) |
    static_cast<std::uint32_t>(lvg::button_mask::back) |
    static_cast<std::uint32_t>(lvg::button_mask::left_stick) |
    static_cast<std::uint32_t>(lvg::button_mask::right_stick) |
    static_cast<std::uint32_t>(lvg::button_mask::left_shoulder) |
    static_cast<std::uint32_t>(lvg::button_mask::right_shoulder) |
    static_cast<std::uint32_t>(lvg::button_mask::home) |
    static_cast<std::uint32_t>(lvg::button_mask::south) |
    static_cast<std::uint32_t>(lvg::button_mask::east) |
    static_cast<std::uint32_t>(lvg::button_mask::west) |
    static_cast<std::uint32_t>(lvg::button_mask::north);

  EXPECT_EQ(platf::vhf::button_mask(flags, false, false), expected);
}

TEST(WindowsVhfButtonTest, MapsTheMiscButtonByProfile) {
  const auto touchpad = static_cast<std::uint32_t>(lvg::button_mask::touchpad);
  const auto home = static_cast<std::uint32_t>(lvg::button_mask::home);

  EXPECT_EQ(platf::vhf::button_mask(MISC_BUTTON, true, false), touchpad);
  EXPECT_EQ(platf::vhf::button_mask(MISC_BUTTON, false, false), home);
  EXPECT_EQ(platf::vhf::button_mask(TOUCHPAD_BUTTON, false, false), touchpad);
  EXPECT_EQ(platf::vhf::button_mask(TOUCHPAD_BUTTON, true, false), touchpad);
}

TEST(WindowsVhfButtonTest, OptionallyClicksTheTouchpadWithTheBackButton) {
  const auto touchpad = static_cast<std::uint32_t>(lvg::button_mask::touchpad);
  const auto back = static_cast<std::uint32_t>(lvg::button_mask::back);

  EXPECT_EQ(platf::vhf::button_mask(BACK, true, false), back);
  EXPECT_EQ(platf::vhf::button_mask(BACK, true, true), back | touchpad);
  EXPECT_EQ(platf::vhf::button_mask(BACK, false, true), back);
}

TEST(WindowsVhfButtonTest, IgnoresUnsupportedFlags) {
  EXPECT_EQ(platf::vhf::button_mask(0, false, false), 0u);
}

TEST(WindowsVhfTouchEventTest, MapsEveryMoonlightTouchEvent) {
  EXPECT_EQ(platf::vhf::map_touch_event(LI_TOUCH_EVENT_HOVER), lvg::touch_event::hover);
  EXPECT_EQ(platf::vhf::map_touch_event(LI_TOUCH_EVENT_DOWN), lvg::touch_event::down);
  EXPECT_EQ(platf::vhf::map_touch_event(LI_TOUCH_EVENT_UP), lvg::touch_event::up);
  EXPECT_EQ(platf::vhf::map_touch_event(LI_TOUCH_EVENT_MOVE), lvg::touch_event::move);
  EXPECT_EQ(platf::vhf::map_touch_event(LI_TOUCH_EVENT_CANCEL), lvg::touch_event::cancel);
  EXPECT_EQ(platf::vhf::map_touch_event(LI_TOUCH_EVENT_CANCEL_ALL), lvg::touch_event::cancel_all);
}

TEST(WindowsVhfTouchEventTest, RejectsUnknownTouchEvents) {
  EXPECT_FALSE(platf::vhf::map_touch_event(0x7F).has_value());
}

TEST(WindowsVhfTouchCoordinateTest, ClampsAndScalesNormalizedCoordinates) {
  EXPECT_EQ(platf::vhf::touch_coordinate(0.0f), 0);
  EXPECT_EQ(platf::vhf::touch_coordinate(0.5f), 32768);
  EXPECT_EQ(platf::vhf::touch_coordinate(1.0f), 65535);
  EXPECT_EQ(platf::vhf::touch_coordinate(-1.0f), 0);
  EXPECT_EQ(platf::vhf::touch_coordinate(2.0f), 65535);
}

TEST(WindowsVhfBatteryTest, MapsEveryMoonlightBatteryState) {
  EXPECT_EQ(platf::vhf::battery_state(LI_BATTERY_STATE_UNKNOWN), lvg::battery_state::unknown);
  EXPECT_EQ(platf::vhf::battery_state(LI_BATTERY_STATE_CHARGING), lvg::battery_state::charging);
  EXPECT_EQ(platf::vhf::battery_state(LI_BATTERY_STATE_DISCHARGING), lvg::battery_state::discharging);
  EXPECT_EQ(platf::vhf::battery_state(LI_BATTERY_STATE_FULL), lvg::battery_state::full);
  EXPECT_EQ(platf::vhf::battery_state(LI_BATTERY_STATE_NOT_PRESENT), lvg::battery_state::not_present);
  EXPECT_EQ(platf::vhf::battery_state(LI_BATTERY_STATE_NOT_CHARGING), lvg::battery_state::not_charging);
}

TEST(WindowsVhfBatteryTest, TreatsUnknownStatesAsUnknown) {
  EXPECT_EQ(platf::vhf::battery_state(0x7F), lvg::battery_state::unknown);
}
#endif
