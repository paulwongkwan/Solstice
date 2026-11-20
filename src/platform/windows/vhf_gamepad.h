/**
 * @file src/platform/windows/vhf_gamepad.h
 * @brief libvirtualgamepad backed Windows virtual gamepad backend.
 */
#pragma once

// platform includes
#include <Windows.h>

// standard includes
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// lib includes
#include <libvirtualgamepad/client.h>

// local includes
#include "src/platform/common.h"

namespace platf {
  /**
   * @brief Helpers that translate the platform contract into the libvirtualgamepad protocol.
   */
  namespace vhf {
    /**
     * @brief Map a configured gamepad name to a libvirtualgamepad profile.
     *
     * @param name Configured gamepad name.
     * @return Profile the driver can create, or no value when the name is unknown to this backend.
     */
    std::optional<lvg::profile> profile_from_name(std::string_view name);

    /**
     * @brief Configuration name that selects a profile.
     *
     * @param profile Driver profile.
     * @return Configured name, or an empty view when this backend has no name for the profile.
     */
    std::string_view name_from_profile(lvg::profile profile);

    /**
     * @brief Whether a profile carries touch, motion and battery state.
     *
     * @param profile Driver profile.
     * @return True for the PlayStation and Switch profiles.
     */
    bool folds_playstation_state(lvg::profile profile);

    /**
     * @brief Translate a Moonlight button mask into the protocol button mask.
     *
     * @param flags Moonlight button mask.
     * @param playstation Whether the created profile is a PlayStation or Switch pad.
     * @param back_as_touchpad_click Whether the back button also clicks the touchpad.
     * @return Protocol button mask.
     */
    std::uint32_t button_mask(std::uint32_t flags, bool playstation, bool back_as_touchpad_click);

    /**
     * @brief Translate a Moonlight touch event into a protocol touch event.
     *
     * @param event_type Moonlight touch event type.
     * @return Protocol touch event, or no value when the event has no equivalent.
     */
    std::optional<lvg::touch_event> map_touch_event(std::uint8_t event_type);

    /**
     * @brief Convert a normalized touch coordinate to the protocol's 16-bit range.
     *
     * @param value Coordinate in the inclusive range `[0.0, 1.0]`.
     * @return Coordinate in the inclusive range `[0, 65535]`.
     */
    std::uint16_t touch_coordinate(float value);

    /**
     * @brief Convert a Moonlight battery state to the protocol battery state.
     *
     * @param state Moonlight battery state.
     * @return Protocol battery state.
     */
    lvg::battery_state battery_state(std::uint8_t state);
  }  // namespace vhf

  /**
   * @brief Virtual gamepad backend for the libvirtualgamepad VHF driver.
   *
   * One instance owns a single control connection, every virtual gamepad the
   * stream created through it, and the thread that polls driver feedback.
   */
  class vhf_gamepad_t {
  public:
    vhf_gamepad_t() = default;
    ~vhf_gamepad_t();

    vhf_gamepad_t(const vhf_gamepad_t &) = delete;
    vhf_gamepad_t &operator=(const vhf_gamepad_t &) = delete;

    /**
     * @brief Connect to the driver and start polling for feedback.
     *
     * @return 0 when the driver is present and speaks the expected protocol; otherwise nonzero.
     */
    int init();

    /**
     * @brief Whether the driver connection is usable.
     *
     * @return True when a gamepad can be created.
     */
    bool available() const noexcept;

    /**
     * @brief Profiles the installed driver accepts.
     *
     * @return Bit mask of accepted profiles.
     */
    lvg::profile_mask_t available_profiles() const noexcept;

    /**
     * @brief Whether a profile is one the installed driver accepts.
     *
     * @param profile Profile to test.
     * @return True when the driver advertises the profile.
     */
    bool supports(lvg::profile profile) const noexcept;

    /**
     * @brief Create a virtual gamepad in a slot.
     *
     * @param id Global and client-relative gamepad identifiers.
     * @param feedback_queue Queue used to return rumble and LED feedback to the client.
     * @param profile Profile to create.
     * @return 0 on success; otherwise -1.
     */
    int alloc_gamepad(const gamepad_id_t &id, feedback_queue_t feedback_queue, lvg::profile profile);

    /**
     * @brief Route an existing virtual gamepad's feedback to a resumed client.
     *
     * @param id Global and client-relative gamepad identifiers.
     * @param feedback_queue Queue used to return feedback to the resumed client.
     * @return 0 when the slot holds a gamepad; otherwise -1.
     */
    int rebind_gamepad(const gamepad_id_t &id, feedback_queue_t feedback_queue);

    /**
     * @brief Destroy the virtual gamepad in a slot.
     *
     * @param nr Global gamepad index.
     */
    void free_gamepad(int nr);

    /**
     * @brief Whether a slot currently holds a virtual gamepad owned by this backend.
     *
     * @param nr Global gamepad index.
     * @return True when the slot is in use.
     */
    bool attached(int nr) const;

    /**
     * @brief Submit the latest button and axis state of a virtual gamepad.
     *
     * @param nr Global gamepad index.
     * @param state Moonlight gamepad state.
     */
    void update_gamepad(int nr, const gamepad_state_t &state);

    /**
     * @brief Submit a touchpad contact lifecycle event.
     *
     * @param touch Touch event reported by the client.
     */
    void update_touch(const gamepad_touch_t &touch);

    /**
     * @brief Submit an accelerometer or gyroscope sample.
     *
     * @param motion Motion sample reported by the client.
     */
    void update_motion(const gamepad_motion_t &motion);

    /**
     * @brief Submit the client's battery state.
     *
     * @param battery Battery state reported by the client.
     */
    void update_battery(const gamepad_battery_t &battery);

  private:
    /**
     * @brief Per-slot virtual gamepad state.
     */
    struct slot_t {
      bool created {};  ///< Whether the driver holds a controller for this slot.
      lvg::profile profile {};  ///< Profile of the created controller.
      lvg::input_state_request input {};  ///< State submitted with the next input request.
      std::map<std::uint32_t, std::uint8_t> pointer_id_map;  ///< Client pointer IDs mapped to contact slots.
      std::array<bool, 2> contacts {};  ///< Whether each contact slot is down.
      std::chrono::steady_clock::time_point last_report_ts {};  ///< Time of the last submitted report.
      std::uint8_t client_relative_index {};  ///< Controller number reported by the client.
      feedback_queue_t feedback_queue;  ///< Queue used to return feedback to the client.
    };

    /**
     * @brief Poll driver feedback until the backend is destroyed.
     */
    void feedback_loop();

    /**
     * @brief Translate one driver feedback event into client feedback messages.
     *
     * @param nr Global gamepad index that produced the event.
     * @param event Feedback event returned by the driver.
     */
    void handle_feedback(int nr, const lvg::feedback_event &event);

    /**
     * @brief Build an initialized request header.
     *
     * @param header Header to initialize.
     * @param size Size of the whole request.
     */
    static void initialize_header(lvg::request_header &header, std::size_t size);

    mutable std::mutex mutex_;  ///< Serializes access to the single control connection.
    lvg::client client_;  ///< Control connection to the driver.
    std::vector<slot_t> slots_;  ///< One entry per global gamepad index.
    std::thread feedback_thread_;  ///< Thread polling driver feedback.
    std::atomic_bool stop_polling_ {false};  ///< Set when the feedback thread must exit.
    bool polling_started_ {};  ///< Whether the feedback thread was started.
  };
}  // namespace platf
