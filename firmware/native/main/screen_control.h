#pragma once

#include "screen_schedule.h"

#include <atomic>
#include <string_view>

namespace espframe::native {

inline constexpr char screen_control_path[] = "/espframe/api/v1/screen";

enum class ScreenAction { Idle, Sleep, Wake };

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
    if (action == ScreenAction::Idle)
      return false;
    auto idle = ScreenAction::Idle;
    return action_.compare_exchange_strong(idle, action, std::memory_order_release, std::memory_order_relaxed);
  }

  bool pending() const { return action_.load(std::memory_order_acquire) != ScreenAction::Idle; }

  bool apply_pending(ScreenSchedule &schedule, int64_t now_us) const {
    switch (action_.load(std::memory_order_acquire)) {
      case ScreenAction::Idle: return false;
      case ScreenAction::Sleep: schedule.sleep(); break;
      case ScreenAction::Wake: schedule.wake(now_us); break;
    }
    return true;
  }

  void completed() { action_.store(ScreenAction::Idle, std::memory_order_release); }

 private:
  std::atomic<ScreenAction> action_{ScreenAction::Idle};
};

inline ScreenControl screen_control;

}  // namespace espframe::native
