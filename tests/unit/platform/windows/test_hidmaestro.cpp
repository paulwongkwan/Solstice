/**
 * @file tests/unit/platform/windows/test_hidmaestro.cpp
 * @brief Tests for the HIDMaestro bridge translation helpers.
 */

// test includes
#include "../../../tests_common.h"

#ifdef _WIN32
  // standard includes
  #include <cstdint>

  // local includes
  #include <src/platform/common.h>
  #include <src/platform/windows/hidmaestro.h>

using namespace platf;

TEST(WindowsHidMaestroProfileTest, MapsEveryConfiguredNameToItsBridgeProfile) {
  EXPECT_EQ(hidmaestro::profile_from_name("x360"), hidmaestro::profile_kind::xbox_360);
  EXPECT_EQ(hidmaestro::profile_from_name("xbox_one"), hidmaestro::profile_kind::xbox_one);
  EXPECT_EQ(hidmaestro::profile_from_name("xbox_series"), hidmaestro::profile_kind::xbox_series);
  EXPECT_EQ(hidmaestro::profile_from_name("ds4"), hidmaestro::profile_kind::dualshock_4);
  EXPECT_EQ(hidmaestro::profile_from_name("dualshock4"), hidmaestro::profile_kind::dualshock_4);
  EXPECT_EQ(hidmaestro::profile_from_name("dualsense"), hidmaestro::profile_kind::dualsense);
  EXPECT_EQ(hidmaestro::profile_from_name("switch_pro"), hidmaestro::profile_kind::switch_pro);
  EXPECT_EQ(hidmaestro::profile_from_name("steam_controller"), hidmaestro::profile_kind::steam_controller);
}

TEST(WindowsHidMaestroProfileTest, LeavesUnknownNamesUnmapped) {
  EXPECT_FALSE(hidmaestro::profile_from_name("auto").has_value());
  EXPECT_FALSE(hidmaestro::profile_from_name("").has_value());
  EXPECT_FALSE(hidmaestro::profile_from_name("vigembus").has_value());
}

TEST(WindowsHidMaestroProfileTest, RoundTripsEveryProfileName) {
  for (const auto profile : {
         hidmaestro::profile_kind::xbox_360,
         hidmaestro::profile_kind::xbox_one,
         hidmaestro::profile_kind::xbox_series,
         hidmaestro::profile_kind::dualshock_4,
         hidmaestro::profile_kind::dualsense,
         hidmaestro::profile_kind::switch_pro,
         hidmaestro::profile_kind::steam_controller,
       }) {
    const auto name = hidmaestro::name_from_profile(profile);
    EXPECT_FALSE(name.empty());
    EXPECT_EQ(hidmaestro::profile_from_name(name), profile);
  }
}

TEST(WindowsHidMaestroProfileTest, OnlyPlayStationLikeProfilesFoldTouchMotionAndBattery) {
  EXPECT_TRUE(hidmaestro::folds_playstation_state(hidmaestro::profile_kind::dualshock_4));
  EXPECT_TRUE(hidmaestro::folds_playstation_state(hidmaestro::profile_kind::dualsense));
  EXPECT_TRUE(hidmaestro::folds_playstation_state(hidmaestro::profile_kind::switch_pro));
  EXPECT_TRUE(hidmaestro::folds_playstation_state(hidmaestro::profile_kind::steam_controller));
  EXPECT_FALSE(hidmaestro::folds_playstation_state(hidmaestro::profile_kind::xbox_360));
  EXPECT_FALSE(hidmaestro::folds_playstation_state(hidmaestro::profile_kind::xbox_one));
  EXPECT_FALSE(hidmaestro::folds_playstation_state(hidmaestro::profile_kind::xbox_series));
}

TEST(WindowsHidMaestroBatteryTest, MapsEveryMoonlightBatteryState) {
  EXPECT_EQ(hidmaestro::battery_flags(LI_BATTERY_STATE_UNKNOWN), 0);
  EXPECT_EQ(hidmaestro::battery_flags(LI_BATTERY_STATE_NOT_PRESENT), 1);
  EXPECT_EQ(hidmaestro::battery_flags(LI_BATTERY_STATE_DISCHARGING), 2);
  EXPECT_EQ(hidmaestro::battery_flags(LI_BATTERY_STATE_CHARGING), 3);
  EXPECT_EQ(hidmaestro::battery_flags(LI_BATTERY_STATE_FULL), 4);
  EXPECT_EQ(hidmaestro::battery_flags(LI_BATTERY_STATE_NOT_CHARGING), 5);
}

TEST(WindowsHidMaestroBatteryTest, TreatsUnknownStatesAsUnknown) {
  EXPECT_EQ(hidmaestro::battery_flags(0x7F), 0);
}
#endif
