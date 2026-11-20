/**
 * @file tests/unit/test_input.cpp
 * @brief Tests for input packet batching, retained stream input, and virtual gamepad lifecycle behavior.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// lib includes
extern "C" {
#include <moonlight-common-c/src/Input.h>
}

// local includes
#include "src/config.h"
#include "src/input.h"
#include "src/utility.h"

namespace {
  /**
   * @brief Protocol metadata for a fixed-size input packet.
   */
  struct input_packet_spec_t {
    std::uint32_t magic;  ///< Packet type identifier.
    std::size_t size;  ///< Complete fixed packet size.
  };

  /**
   * @brief Create raw input bytes with a caller-selected header and buffer size.
   *
   * @param magic Packet type identifier.
   * @param declared_size Packet size declared after the size field.
   * @param actual_size Number of bytes available in the packet buffer.
   * @return Raw packet bytes.
   */
  std::vector<std::uint8_t> make_input_packet(std::uint32_t magic, std::uint32_t declared_size, std::size_t actual_size) {
    std::vector<std::uint8_t> packet(actual_size);
    const NV_INPUT_HEADER header {
      util::endian::big(declared_size),
      util::endian::little(magic),
    };
    if (!packet.empty()) {
      std::memcpy(packet.data(), &header, std::min(packet.size(), sizeof(header)));
    }
    return packet;
  }

  /**
   * @brief Verify batching a repeated packet around a potentially different packet.
   *
   * @tparam Packet Fixed-size protocol packet type.
   * @param original Packet repeated before and after the different packet.
   * @param different Packet that may introduce a batching boundary.
   * @param magic Protocol identifier shared by the packets.
   * @param expected Expected number of dispatched packets.
   */
  template<class Packet>
  void expect_packet_dispatches(Packet original, Packet different, std::uint32_t magic, std::size_t expected) {
    SCOPED_TRACE(magic);
    auto stream_input = input::alloc(std::make_shared<safe::mail_raw_t>(), "packet-batching");
    ASSERT_NE(stream_input, nullptr);
    std::size_t dispatch_count = 0;
    input::testing::set_input_packet_hook([magic, &dispatch_count](std::uint32_t dispatched_magic) {
      EXPECT_EQ(dispatched_magic, magic);
      ++dispatch_count;
    });
    for (auto packet : {original, different, original}) {
      packet.header.size = util::endian::big<std::uint32_t>(sizeof(packet) - sizeof(packet.header.size));
      packet.header.magic = util::endian::little(magic);
      std::vector<std::uint8_t> bytes(sizeof(packet));
      std::memcpy(bytes.data(), &packet, sizeof(packet));
      input::passthrough(stream_input, std::move(bytes));
    }
    input::testing::process_queued_messages(stream_input);
    input::testing::set_input_packet_hook({});
    EXPECT_EQ(dispatch_count, expected);
    EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), 0);
  }

  /**
   * @brief Fixture that isolates configuration, retained sessions, and test hooks.
   */
  class InputSessionTest: public ::testing::Test {
  protected:
    /**
     * @brief Preserve configuration and clear keyboard state left by an earlier test.
     */
    void SetUp() override {
      original_input_ = config::input;
      config::input.controller = true;
      config::input.mouse = true;
      config::input.gamepad = "xseries";
      input::testing::reset_keyboard_state();

      auto platform_input = platf::input();
      ASSERT_TRUE(platform_input);
      input::testing::set_platform_input(std::move(platform_input));
    }

    /**
     * @brief Remove test hooks, destroy retained sessions, and restore configuration.
     */
    void TearDown() override {
      input::testing::set_input_packet_hook({});
      input::testing::set_input_task_sink({});
      input::terminate_gamepads();
      input::testing::set_platform_input({});
      config::input = std::move(original_input_);
    }

  private:
    config::input_t original_input_;  ///< Input configuration restored after each test.
  };
}  // namespace

TEST(InputPacketValidationTest, RejectsEveryBufferShorterThanTheHeader) {
  for (std::size_t actual_size = 0; actual_size < sizeof(NV_INPUT_HEADER); ++actual_size) {
    const auto packet = make_input_packet(UTF8_TEXT_EVENT_MAGIC, sizeof(std::uint32_t), actual_size);
    EXPECT_FALSE(input::testing::is_valid_input_packet(packet)) << "actual_size=" << actual_size;
  }
}

TEST(InputPacketValidationTest, RejectsDeclaredSizesOutsideTheAvailableBuffer) {
  constexpr std::uint32_t unknown_magic = 0x12345678;
  for (std::uint32_t declared_size = 0; declared_size < sizeof(std::uint32_t); ++declared_size) {
    const auto packet = make_input_packet(unknown_magic, declared_size, sizeof(NV_INPUT_HEADER));
    EXPECT_FALSE(input::testing::is_valid_input_packet(packet)) << "declared_size=" << declared_size;
  }

  const auto truncated_packet = make_input_packet(unknown_magic, sizeof(std::uint32_t) + 1, sizeof(NV_INPUT_HEADER));
  EXPECT_FALSE(input::testing::is_valid_input_packet(truncated_packet));

  const auto overflowing_size = make_input_packet(
    unknown_magic,
    std::numeric_limits<std::uint32_t>::max(),
    sizeof(NV_INPUT_HEADER)
  );
  EXPECT_FALSE(input::testing::is_valid_input_packet(overflowing_size));
}

TEST(InputPacketValidationTest, ValidatesUnicodePacketBoundsWithoutOverflow) {
  const auto empty_packet = make_input_packet(UTF8_TEXT_EVENT_MAGIC, sizeof(std::uint32_t), sizeof(NV_INPUT_HEADER));
  EXPECT_TRUE(input::testing::is_valid_input_packet(empty_packet));

  constexpr std::uint32_t maximum_declared_size = sizeof(std::uint32_t) + UTF8_TEXT_EVENT_MAX_COUNT;
  const auto maximum_packet = make_input_packet(
    UTF8_TEXT_EVENT_MAGIC,
    maximum_declared_size,
    sizeof(std::uint32_t) + maximum_declared_size
  );
  EXPECT_TRUE(input::testing::is_valid_input_packet(maximum_packet));

  const auto oversized_text = make_input_packet(
    UTF8_TEXT_EVENT_MAGIC,
    maximum_declared_size + 1,
    sizeof(std::uint32_t) + maximum_declared_size + 1
  );
  EXPECT_FALSE(input::testing::is_valid_input_packet(oversized_text));

  const auto truncated_text = make_input_packet(UTF8_TEXT_EVENT_MAGIC, maximum_declared_size, sizeof(NV_INPUT_HEADER));
  EXPECT_FALSE(input::testing::is_valid_input_packet(truncated_text));
}

TEST(InputPacketValidationTest, EnforcesEveryFixedPacketSize) {
  constexpr std::array packet_specs {
    input_packet_spec_t {MOUSE_MOVE_REL_MAGIC_GEN5, sizeof(NV_REL_MOUSE_MOVE_PACKET)},
    input_packet_spec_t {MOUSE_MOVE_ABS_MAGIC, sizeof(NV_ABS_MOUSE_MOVE_PACKET)},
    input_packet_spec_t {MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5, sizeof(NV_MOUSE_BUTTON_PACKET)},
    input_packet_spec_t {MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5, sizeof(NV_MOUSE_BUTTON_PACKET)},
    input_packet_spec_t {SCROLL_MAGIC_GEN5, sizeof(NV_SCROLL_PACKET)},
    input_packet_spec_t {SS_HSCROLL_MAGIC, sizeof(SS_HSCROLL_PACKET)},
    input_packet_spec_t {KEY_DOWN_EVENT_MAGIC, sizeof(NV_KEYBOARD_PACKET)},
    input_packet_spec_t {KEY_UP_EVENT_MAGIC, sizeof(NV_KEYBOARD_PACKET)},
    input_packet_spec_t {MULTI_CONTROLLER_MAGIC_GEN5, sizeof(NV_MULTI_CONTROLLER_PACKET)},
    input_packet_spec_t {SS_TOUCH_MAGIC, sizeof(SS_TOUCH_PACKET)},
    input_packet_spec_t {SS_PEN_MAGIC, sizeof(SS_PEN_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_ARRIVAL_MAGIC, sizeof(SS_CONTROLLER_ARRIVAL_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_TOUCH_MAGIC, sizeof(SS_CONTROLLER_TOUCH_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_MOTION_MAGIC, sizeof(SS_CONTROLLER_MOTION_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_BATTERY_MAGIC, sizeof(SS_CONTROLLER_BATTERY_PACKET)},
  };

  for (const auto &[magic, size] : packet_specs) {
    const auto declared_size = static_cast<std::uint32_t>(size - sizeof(std::uint32_t));
    const auto valid_packet = make_input_packet(magic, declared_size, size);
    EXPECT_TRUE(input::testing::is_valid_input_packet(valid_packet)) << "magic=" << magic;

    const auto trailing_bytes = make_input_packet(magic, declared_size, size + 8);
    EXPECT_TRUE(input::testing::is_valid_input_packet(trailing_bytes)) << "magic=" << magic;

    const auto wrong_declared_size = make_input_packet(magic, declared_size - 1, size);
    EXPECT_FALSE(input::testing::is_valid_input_packet(wrong_declared_size)) << "magic=" << magic;

    const auto oversized_declared_size = make_input_packet(magic, declared_size + 1, size + 1);
    EXPECT_FALSE(input::testing::is_valid_input_packet(oversized_declared_size)) << "magic=" << magic;

    const auto truncated_packet = make_input_packet(magic, declared_size, size - 1);
    EXPECT_FALSE(input::testing::is_valid_input_packet(truncated_packet)) << "magic=" << magic;
  }
}

TEST(InputPacketValidationTest, PreservesUnknownPacketHandlingWithinDeclaredBounds) {
  constexpr std::uint32_t unknown_magic = 0x12345678;
  const auto packet = make_input_packet(unknown_magic, sizeof(std::uint32_t), sizeof(NV_INPUT_HEADER));
  EXPECT_TRUE(input::testing::is_valid_input_packet(packet));
}

TEST_F(InputSessionTest, RejectsMalformedBatchablePacketsAtQueueIngress) {
  ASSERT_FALSE(task_pool.running());
  const std::shared_ptr<input::input_t> empty_input;
  EXPECT_EQ(input::testing::queued_input_packet_count(empty_input), 0);

  auto stream_input = input::alloc(std::make_shared<safe::mail_raw_t>(), "packet-validation-client");
  ASSERT_NE(stream_input, nullptr);

  auto short_header = make_input_packet(SS_PEN_MAGIC, sizeof(std::uint32_t), sizeof(NV_INPUT_HEADER) - 1);
  input::passthrough(stream_input, std::move(short_header));
  EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), 0);

  constexpr std::array batchable_packet_specs {
    input_packet_spec_t {MOUSE_MOVE_REL_MAGIC_GEN5, sizeof(NV_REL_MOUSE_MOVE_PACKET)},
    input_packet_spec_t {MOUSE_MOVE_ABS_MAGIC, sizeof(NV_ABS_MOUSE_MOVE_PACKET)},
    input_packet_spec_t {SCROLL_MAGIC_GEN5, sizeof(NV_SCROLL_PACKET)},
    input_packet_spec_t {SS_HSCROLL_MAGIC, sizeof(SS_HSCROLL_PACKET)},
    input_packet_spec_t {MULTI_CONTROLLER_MAGIC_GEN5, sizeof(NV_MULTI_CONTROLLER_PACKET)},
    input_packet_spec_t {SS_TOUCH_MAGIC, sizeof(SS_TOUCH_PACKET)},
    input_packet_spec_t {SS_PEN_MAGIC, sizeof(SS_PEN_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_TOUCH_MAGIC, sizeof(SS_CONTROLLER_TOUCH_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_MOTION_MAGIC, sizeof(SS_CONTROLLER_MOTION_PACKET)},
  };

  std::size_t valid_packet_count = 0;
  for (const auto &[magic, size] : batchable_packet_specs) {
    const auto declared_size = static_cast<std::uint32_t>(size - sizeof(std::uint32_t));

    auto truncated_packet = make_input_packet(magic, declared_size, size - 1);
    input::passthrough(stream_input, std::move(truncated_packet));
    EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), valid_packet_count) << "magic=" << magic;

    auto valid_packet = make_input_packet(magic, declared_size, size);
    input::passthrough(stream_input, std::move(valid_packet));
    ++valid_packet_count;
    EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), valid_packet_count) << "magic=" << magic;

    auto oversized_declared_size = make_input_packet(magic, declared_size + 1, size + 1);
    input::passthrough(stream_input, std::move(oversized_declared_size));
    EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), valid_packet_count) << "magic=" << magic;
  }
}

TEST_F(InputSessionTest, SerializesArrivalAndStatePacketsForOneController) {
  ASSERT_FALSE(task_pool.running());
  config::input.gamepad = "auto";

  auto stream_input = input::alloc(std::make_shared<safe::mail_raw_t>(), "simultaneous-controller-packets");
  ASSERT_NE(stream_input, nullptr);
  std::vector<std::shared_ptr<input::input_t>> scheduled;
  input::testing::set_input_task_sink([&](std::shared_ptr<input::input_t> input) {
    scheduled.push_back(std::move(input));
  });

  SS_CONTROLLER_ARRIVAL_PACKET arrival {};
  arrival.header.size = util::endian::big<std::uint32_t>(sizeof(arrival) - sizeof(arrival.header.size));
  arrival.header.magic = util::endian::little(SS_CONTROLLER_ARRIVAL_MAGIC);
  arrival.controllerNumber = 0;
  arrival.type = LI_CTYPE_NINTENDO;

  NV_MULTI_CONTROLLER_PACKET state {};
  state.header.size = util::endian::big<std::uint32_t>(sizeof(state) - sizeof(state.header.size));
  state.header.magic = util::endian::little(MULTI_CONTROLLER_MAGIC_GEN5);
  state.controllerNumber = 0;
  state.activeGamepadMask = 1;

  std::vector<std::uint8_t> arrival_bytes(sizeof(arrival));
  std::memcpy(arrival_bytes.data(), &arrival, sizeof(arrival));
  input::passthrough(stream_input, std::move(arrival_bytes));
  ASSERT_EQ(scheduled.size(), 1);
  EXPECT_EQ(scheduled.front(), stream_input);

  std::promise<void> arrival_started;
  std::promise<void> release_arrival;
  const auto release_signal = release_arrival.get_future().share();
  input::testing::set_input_packet_hook([&](std::uint32_t magic) {
    if (magic == SS_CONTROLLER_ARRIVAL_MAGIC) {
      arrival_started.set_value();
      release_signal.wait();
    }
  });

  std::jthread worker {input::testing::process_queued_messages, stream_input};
  const auto arrival_status = arrival_started.get_future().wait_for(std::chrono::seconds {5});
  if (arrival_status != std::future_status::ready) {
    release_arrival.set_value();
    worker.join();
    FAIL() << "Controller arrival packet did not reach the dispatch hook";
  }

  std::vector<std::uint8_t> state_bytes(sizeof(state));
  std::memcpy(state_bytes.data(), &state, sizeof(state));
  input::passthrough(stream_input, std::move(state_bytes));
  EXPECT_EQ(scheduled.size(), 1);
  EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), 1);

  auto other_input = input::alloc(std::make_shared<safe::mail_raw_t>(), "independent-input-stream");
  if (!other_input) {
    release_arrival.set_value();
    worker.join();
    FAIL() << "Could not create independent input stream";
  }
  input::passthrough(other_input, make_input_packet(0x12345678, sizeof(std::uint32_t), sizeof(NV_INPUT_HEADER)));
  EXPECT_EQ(scheduled.size(), 2);
  if (scheduled.size() == 2) {
    EXPECT_EQ(scheduled.back(), other_input);
  }
  release_arrival.set_value();
  worker.join();
  input::testing::set_input_packet_hook({});
  input::testing::process_queued_messages(other_input);

  EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), 0);
  EXPECT_EQ(input::testing::queued_input_packet_count(other_input), 0);
}

TEST_F(InputSessionTest, YieldsAfterBoundedPacketBatch) {
  ASSERT_FALSE(task_pool.running());
  auto stream_input = input::alloc(std::make_shared<safe::mail_raw_t>(), "bounded-input-batch");
  ASSERT_NE(stream_input, nullptr);

  std::vector<std::shared_ptr<input::input_t>> scheduled;
  input::testing::set_input_task_sink([&](std::shared_ptr<input::input_t> input) {
    scheduled.push_back(std::move(input));
  });

  constexpr std::uint32_t unknown_magic = 0x12345678;
  constexpr std::size_t packet_count = 33;
  for (std::size_t n = 0; n < packet_count; ++n) {
    input::passthrough(stream_input, make_input_packet(unknown_magic, sizeof(std::uint32_t), sizeof(NV_INPUT_HEADER)));
  }
  ASSERT_EQ(scheduled.size(), 1);

  input::testing::process_queued_messages(scheduled.front());
  EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), 1);
  ASSERT_EQ(scheduled.size(), 2);
  input::testing::process_queued_messages(scheduled.back());
  EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), 0);
  EXPECT_EQ(scheduled.size(), 2);
}

TEST_F(InputSessionTest, BatchesContinuousPacketsWithoutCrossingStateChanges) {
  ASSERT_FALSE(task_pool.running());
  config::input.mouse = false;
  config::input.controller = false;
  config::input.native_pen_touch = false;
  input::testing::set_input_task_sink([](std::shared_ptr<input::input_t>) {
  });

  NV_MULTI_CONTROLLER_PACKET controller {};
  expect_packet_dispatches(controller, controller, MULTI_CONTROLLER_MAGIC_GEN5, 1);
  auto changed_controller = controller;
  changed_controller.controllerNumber = 1;
  expect_packet_dispatches(controller, changed_controller, MULTI_CONTROLLER_MAGIC_GEN5, 2);
  changed_controller = controller;
  changed_controller.activeGamepadMask = 1;
  expect_packet_dispatches(controller, changed_controller, MULTI_CONTROLLER_MAGIC_GEN5, 3);
  changed_controller = controller;
  changed_controller.buttonFlags = 1;
  expect_packet_dispatches(controller, changed_controller, MULTI_CONTROLLER_MAGIC_GEN5, 3);
  changed_controller = controller;
  changed_controller.buttonFlags2 = 1;
  expect_packet_dispatches(controller, changed_controller, MULTI_CONTROLLER_MAGIC_GEN5, 3);

  SS_TOUCH_PACKET touch {};
  touch.eventType = LI_TOUCH_EVENT_MOVE;
  expect_packet_dispatches(touch, touch, SS_TOUCH_MAGIC, 1);
  auto changed_touch = touch;
  changed_touch.pointerId = 1;
  expect_packet_dispatches(touch, changed_touch, SS_TOUCH_MAGIC, 2);
  changed_touch = touch;
  changed_touch.eventType = LI_TOUCH_EVENT_DOWN;
  expect_packet_dispatches(touch, changed_touch, SS_TOUCH_MAGIC, 3);
  changed_touch.eventType = LI_TOUCH_EVENT_HOVER;
  expect_packet_dispatches(touch, changed_touch, SS_TOUCH_MAGIC, 3);

  SS_PEN_PACKET pen {};
  pen.eventType = LI_TOUCH_EVENT_MOVE;
  expect_packet_dispatches(pen, pen, SS_PEN_MAGIC, 1);
  auto changed_pen = pen;
  changed_pen.eventType = LI_TOUCH_EVENT_DOWN;
  expect_packet_dispatches(pen, changed_pen, SS_PEN_MAGIC, 3);
  changed_pen.eventType = LI_TOUCH_EVENT_HOVER;
  expect_packet_dispatches(pen, changed_pen, SS_PEN_MAGIC, 3);
  changed_pen = pen;
  changed_pen.penButtons = 1;
  expect_packet_dispatches(pen, changed_pen, SS_PEN_MAGIC, 3);
  changed_pen = pen;
  changed_pen.toolType = 1;
  expect_packet_dispatches(pen, changed_pen, SS_PEN_MAGIC, 3);

  SS_CONTROLLER_TOUCH_PACKET controller_touch {};
  controller_touch.eventType = LI_TOUCH_EVENT_MOVE;
  expect_packet_dispatches(controller_touch, controller_touch, SS_CONTROLLER_TOUCH_MAGIC, 1);
  auto changed_controller_touch = controller_touch;
  changed_controller_touch.controllerNumber = 1;
  expect_packet_dispatches(controller_touch, changed_controller_touch, SS_CONTROLLER_TOUCH_MAGIC, 2);
  changed_controller_touch = controller_touch;
  changed_controller_touch.pointerId = 1;
  expect_packet_dispatches(controller_touch, changed_controller_touch, SS_CONTROLLER_TOUCH_MAGIC, 2);
  changed_controller_touch = controller_touch;
  changed_controller_touch.eventType = LI_TOUCH_EVENT_DOWN;
  expect_packet_dispatches(controller_touch, changed_controller_touch, SS_CONTROLLER_TOUCH_MAGIC, 3);
  changed_controller_touch.eventType = LI_TOUCH_EVENT_HOVER;
  expect_packet_dispatches(controller_touch, changed_controller_touch, SS_CONTROLLER_TOUCH_MAGIC, 3);

  SS_CONTROLLER_MOTION_PACKET motion {};
  expect_packet_dispatches(motion, motion, SS_CONTROLLER_MOTION_MAGIC, 1);
  auto changed_motion = motion;
  changed_motion.controllerNumber = 1;
  expect_packet_dispatches(motion, changed_motion, SS_CONTROLLER_MOTION_MAGIC, 2);
  changed_motion = motion;
  changed_motion.motionType = 1;
  expect_packet_dispatches(motion, changed_motion, SS_CONTROLLER_MOTION_MAGIC, 2);
}

