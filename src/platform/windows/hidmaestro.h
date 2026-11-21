/**
 * @file src/platform/windows/hidmaestro.h
 * @brief HIDMaestro backed Windows virtual gamepad backend.
 */
#pragma once

// platform includes
#include <Windows.h>

// standard includes
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// local includes
#include "src/platform/common.h"

namespace platf {
  /**
   * @brief Helpers that translate the platform contract into the HIDMaestro bridge protocol.
   */
  namespace hidmaestro {
    /**
     * @brief Virtual controller identities the bridge can create.
     */
    enum class profile_kind : std::uint32_t {
      xbox_360 = 1,  ///< Xbox 360 wired controller.
      xbox_one = 2,  ///< Xbox One S controller.
      xbox_series = 3,  ///< Xbox Series X|S controller.
      dualshock_4 = 4,  ///< DualShock 4 v2 controller.
      dualsense = 5,  ///< DualSense controller.
      switch_pro = 6,  ///< Nintendo Switch Pro controller.
      steam_controller = 7,  ///< Steam Controller (2026).
    };

    /**
     * @brief Map a configured gamepad name to a bridge profile.
     *
     * @param name Configured gamepad name.
     * @return Profile the bridge can create, or no value when the name is unknown to this backend.
     */
    std::optional<profile_kind> profile_from_name(std::string_view name);

    /**
     * @brief Configuration name that selects a profile.
     *
     * @param profile Bridge profile.
     * @return Configured name, or an empty view when this backend has no name for the profile.
     */
    std::string_view name_from_profile(profile_kind profile);

    /**
     * @brief Whether a profile carries touch, motion and battery state.
     *
     * @param profile Bridge profile.
     * @return True for the PlayStation, Switch and Steam profiles.
     */
    bool folds_playstation_state(profile_kind profile);

    /**
     * @brief Convert the client's battery state into the bridge's flag byte.
     *
     * @param state Moonlight battery state.
     * @return Bridge battery flag.
     */
    std::uint8_t battery_flags(std::uint8_t state);
  }  // namespace hidmaestro

  /**
   * @brief Virtual gamepad backend that drives the HIDMaestro SDK through its bridge process.
   *
   * HIDMaestro is a managed SDK, so one helper process owns the SDK context and
   * talks to Solstice over a named pipe. This class owns that process, the pipe,
   * the reader thread that forwards rumble and lightbar feedback, and one entry
   * per global gamepad slot.
   */
  class hidmaestro_t {
  public:
    hidmaestro_t() = default;
    ~hidmaestro_t();

    hidmaestro_t(const hidmaestro_t &) = delete;
    hidmaestro_t &operator=(const hidmaestro_t &) = delete;

    /**
     * @brief Start the bridge and wait for its handshake.
     *
     * @return 0 when the bridge created a controller-capable session; otherwise nonzero.
     */
    int init();

    /**
     * @brief Whether the bridge completed its handshake and can create controllers.
     *
     * @return True when gamepads can be created.
     */
    bool available() const noexcept;

    /**
     * @brief Human-readable reason the bridge is unavailable.
     *
     * @return Status message reported during the handshake.
     */
    std::string status_message() const;

    /**
     * @brief Create a virtual gamepad in a slot.
     *
     * @param id Global and client-relative gamepad identifiers.
     * @param feedback_queue Queue used to return rumble and LED feedback to the client.
     * @param profile Profile to create.
     * @return 0 on success; otherwise -1.
     */
    int alloc_gamepad(const gamepad_id_t &id, feedback_queue_t feedback_queue, hidmaestro::profile_kind profile);

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
     * @brief Whether a slot currently holds a gamepad owned by this backend.
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
     * @brief Per-slot bridge state.
     */
    struct slot_t {
      bool created {};  ///< Whether the bridge holds a controller for this slot.
      hidmaestro::profile_kind profile {};  ///< Profile of the created controller.
      std::map<std::uint32_t, std::uint8_t> pointer_id_map;  ///< Client pointer IDs mapped to contacts.
      std::uint8_t client_relative_index {};  ///< Controller number reported by the client.
      feedback_queue_t feedback_queue;  ///< Queue used to return feedback to the client.
    };

    /**
     * @brief Write one framed message to the bridge.
     *
     * @param type Message type.
     * @param payload Message payload.
     * @return True when the message was written.
     */
    bool write_message(std::int32_t type, const std::vector<std::uint8_t> &payload);

    /**
     * @brief Read bridge messages until the pipe closes.
     */
    void reader_loop();

    /**
     * @brief Forward one rumble message to the client.
     *
     * @param payload Message payload.
     */
    void handle_rumble(const std::vector<std::uint8_t> &payload);

    /**
     * @brief Forward one lightbar message to the client.
     *
     * @param payload Message payload.
     */
    void handle_led(const std::vector<std::uint8_t> &payload);

    /**
     * @brief Record the result of an awaited slot operation.
     *
     * @param payload Message payload.
     */
    void handle_ack(const std::vector<std::uint8_t> &payload);

    /**
     * @brief Stop the bridge process and release the pipe.
     */
    void shutdown() noexcept;

    mutable std::mutex mutex_;  ///< Serializes writes to the bridge and slot state.
    std::mutex ack_mutex_;  ///< Guards the pending acknowledgement.
    std::condition_variable ack_condition_;  ///< Signals the result of a slot operation.
    int pending_ack_slot_ {-1};  ///< Slot whose acknowledgement is awaited.
    int pending_ack_status_ {};  ///< Status of the awaited acknowledgement.
    HANDLE pipe_ {INVALID_HANDLE_VALUE};  ///< Pipe connected to the bridge.
    HANDLE process_ {};  ///< Bridge process handle.
    std::thread reader_thread_;  ///< Thread forwarding bridge feedback.
    std::vector<slot_t> slots_;  ///< One entry per global gamepad index.
    bool available_ {};  ///< Whether the handshake succeeded.
    std::string status_message_;  ///< Handshake status message.
  };
}  // namespace platf
