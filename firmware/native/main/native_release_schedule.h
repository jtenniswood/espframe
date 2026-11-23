#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace espframe::native {

class NativeReleaseSchedule {
 public:
  void configure(int64_t now, bool enabled, std::string_view frequency) {
    const auto cadence = interval(frequency);
    if (!cadence_)
      next_ = now + hour;
    else if (enabled && !enabled_)
      next_ = now;
    else if (cadence != cadence_)
      next_ = now + cadence;
    enabled_ = enabled;
    cadence_ = cadence;
  }

  bool due(int64_t now) const { return enabled_ && now >= next_; }

  void checked(int64_t now, bool reached_feed) {
    next_ = now + (reached_feed ? cadence_ : std::min(cadence_, hour / 4));
  }

 private:
  static constexpr int64_t hour = 3'600'000'000;

  static constexpr int64_t interval(std::string_view frequency) {
    if (frequency == "Hourly") return hour;
    if (frequency == "Weekly") return 168 * hour;
    if (frequency == "Monthly") return 720 * hour;
    return 24 * hour;
  }

  int64_t cadence_{};
  int64_t next_{};
  bool enabled_{};
};

}  // namespace espframe::native
