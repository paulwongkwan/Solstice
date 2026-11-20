/**
 * @file tests/unit/test_encode_device.cpp
 * @brief Tests for typed AVCodec hardware device initialization.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <array>
#include <type_traits>

// local includes
#include <src/audio.h>
#include <src/video.h>

static_assert(std::is_same_v<audio::packet_t::first_type, stream::session_t *>);
static_assert(std::is_same_v<decltype(video::packet_raw_t::channel_data), stream::session_t *>);

TEST(EncodeDeviceStateTest, SoftwareDeviceDoesNotAdvertiseHardwareResources) {
  platf::avcodec_encode_device_t device;
  EXPECT_FALSE(device.hardware);
  EXPECT_EQ(device.init_hw_device, nullptr);
  EXPECT_EQ(device.d3d11_device, nullptr);
}

