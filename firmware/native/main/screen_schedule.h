#pragma once

#include <cstdint>
#include <optional>

namespace espframe::native {

struct ScreenScheduleSettings {
  bool enabled{};
  uint8_t on_hour{6};
  uint8_t off_hour{23};
  uint32_t wake_seconds{60};
};

class ScreenSchedule {
 public:
  explicit ScreenSchedule(ScreenScheduleSettings settings) : settings_(settings) {}

  void sleep() {
    manually_asleep_ = true;
    awake_ = false;
    wake_until_ = 0;
  }

  void wake(int64_t now_us) {
    manually_asleep_ = false;
    awake_ = true;
    wake_until_ = now_us + settings_.wake_seconds * 1'000'000LL;
  }

  bool update(std::optional<int> hour, int64_t now_us, bool interacting) {
    if (manually_asleep_)
      return false;
    if (!settings_.enabled || !hour || in_window_(*hour)) {
      wake_until_ = 0;
      awake_ = true;
    } else {
      // Finish a gesture already in progress before turning the screen off.
      awake_ = now_us < wake_until_ || (awake_ && interacting);
    }
    return awake_;
  }

 private:
  bool in_window_(int hour) const {
    if (settings_.on_hour == settings_.off_hour)
      return true;
    if (settings_.on_hour < settings_.off_hour)
      return hour >= settings_.on_hour && hour < settings_.off_hour;
    return hour >= settings_.on_hour || hour < settings_.off_hour;
  }

  ScreenScheduleSettings settings_;
  bool awake_{true};
  bool manually_asleep_{};
  int64_t wake_until_{};
};

}  // namespace espframe::native
