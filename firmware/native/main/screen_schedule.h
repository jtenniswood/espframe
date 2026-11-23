#pragma once

#include <cstdint>
#include <optional>

namespace espframe::native {

struct ScreenScheduleSettings {
  bool enabled{};
  uint8_t on_hour{6};
  uint8_t off_hour{23};
  uint32_t wake_seconds{60};

  // A single 32-bit value keeps all four settings in sync across cores.
  constexpr uint32_t packed() const {
    return (wake_seconds << 11) | (uint32_t{off_hour} << 6) | (uint32_t{on_hour} << 1) | enabled;
  }

  static constexpr ScreenScheduleSettings unpack(uint32_t value) {
    return {static_cast<bool>(value & 1), static_cast<uint8_t>((value >> 1) & 31),
            static_cast<uint8_t>((value >> 6) & 31), value >> 11};
  }
};

class ScreenSchedule {
 public:
  explicit ScreenSchedule(ScreenScheduleSettings settings) : settings_(settings) {}

  void set_settings(ScreenScheduleSettings settings) {
    if (settings.packed() == settings_.packed())
      return;
    if (settings_.enabled && !settings.enabled) {
      manually_asleep_ = false;
      wake_until_ = 0;
    } else if (wake_until_) {
      // A changed duration is measured from the original touch wake.
      wake_until_ += (static_cast<int64_t>(settings.wake_seconds) - settings_.wake_seconds) * 1'000'000;
    }
    settings_ = settings;
  }

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

  bool wake_for_backlight(std::optional<int> hour, int64_t now_us) {
    if (settings_.enabled && hour && !in_window_(*hour) && now_us >= wake_until_)
      return false;
    manually_asleep_ = false;
    awake_ = true;
    return true;
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
