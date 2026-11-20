#pragma once

#include "screen_schedule.h"

#include <atomic>
#include <optional>
#include <string_view>

namespace espframe::native {

inline constexpr char screen_control_path[] = "/espframe/api/v1/screen";

enum class ScreenAction : uint8_t { Idle, Sleep, Wake, AdjustBacklight };

constexpr ScreenAction screen_action(std::string_view form) {
  if (form == "action=sleep")
    return ScreenAction::Sleep;
  if (form == "action=wake")
    return ScreenAction::Wake;
  return ScreenAction::Idle;
}

// Only the player touches the schedule. The slot stays occupied until it has
// applied the backlight and published the resulting screen state.
class ScreenControl {
 public:
  bool submit(ScreenAction action) {
    if (action != ScreenAction::Sleep && action != ScreenAction::Wake)
      return false;
    return submit_(static_cast<uint32_t>(action));
  }

  bool submit_backlight(std::optional<uint8_t> level) {
    if (level && *level == 0)
      return submit(ScreenAction::Sleep);
    if (level && *level > 100)
      return false;
    return submit_(static_cast<uint32_t>(ScreenAction::AdjustBacklight) |
                   (static_cast<uint32_t>(level.value_or(0)) << 8));
  }

  bool pending() const { return request_.load(std::memory_order_acquire) != 0; }
  uint8_t backlight() const { return backlight_.load(std::memory_order_relaxed); }
  void publish_backlight(uint8_t level) { backlight_.store(level, std::memory_order_relaxed); }

  bool apply_pending(ScreenSchedule &schedule, int64_t now_us, std::optional<int> hour,
                     std::optional<uint8_t> &manual_brightness) const {
    const auto request = request_.load(std::memory_order_acquire);
    switch (static_cast<ScreenAction>(request & 0xff)) {
      case ScreenAction::Idle: return false;
      case ScreenAction::Sleep:
        manual_brightness.reset();
        schedule.sleep();
        break;
      case ScreenAction::Wake:
        manual_brightness.reset();
        schedule.wake(now_us);
        break;
      case ScreenAction::AdjustBacklight:
        // Preserve an existing timed wake's deadline when adjusting the light.
        if (schedule.wake_for_backlight(hour, now_us)) {
          if (const auto level = static_cast<uint8_t>(request >> 8))
            manual_brightness = level;
        }
        break;
    }
    return true;
  }

  void completed() { request_.store(0, std::memory_order_release); }

 private:
  bool submit_(uint32_t request) {
    uint32_t idle = 0;
    return request_.compare_exchange_strong(idle, request, std::memory_order_release, std::memory_order_relaxed);
  }

  std::atomic<uint32_t> request_{0};
  std::atomic<uint8_t> backlight_{0};
};

inline ScreenControl screen_control;

}  // namespace espframe::native
