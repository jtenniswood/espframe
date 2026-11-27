#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace espframe::native {

class NativeReleaseSchedule {
 public:
  explicit NativeReleaseSchedule(int64_t initial_delay = hour, int64_t retry_delay = hour / 4,
                                 int64_t fixed_cadence = 0)
      : initial_delay_(initial_delay), retry_delay_(retry_delay), fixed_cadence_(fixed_cadence) {}

  void configure(int64_t now, bool enabled, std::string_view frequency) {
    const auto cadence = fixed_cadence_ ? fixed_cadence_ : interval(frequency);
    if (!cadence_)
      next_ = now + initial_delay_;
    else if (enabled && !enabled_)
      next_ = now;
    else if (cadence != cadence_)
      next_ = now + cadence;
    enabled_ = enabled;
    cadence_ = cadence;
  }

  bool due(int64_t now) const { return enabled_ && now >= next_; }

  void checked(int64_t now, bool reached_feed) {
    next_ = now + (reached_feed ? cadence_ : std::min(cadence_, retry_delay_));
  }

 private:
  static constexpr int64_t hour = 3'600'000'000;

  static constexpr int64_t interval(std::string_view frequency) {
    if (frequency == "Hourly") return hour;
    if (frequency == "Weekly") return 168 * hour;
    if (frequency == "Monthly") return 720 * hour;
    return 24 * hour;
  }

  const int64_t initial_delay_;
  const int64_t retry_delay_;
  const int64_t fixed_cadence_;
  int64_t cadence_{};
  int64_t next_{};
  bool enabled_{};
};

}  // namespace espframe::native
