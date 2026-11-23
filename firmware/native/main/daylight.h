#pragma once

#include "clock_settings.h"

namespace espframe::native {

struct SolarTimes {
  int sunrise;
  int sunset;
  bool operator==(const SolarTimes &) const = default;
};

class Daylight {
 public:
  explicit Daylight(const ClockZone *zone) : zone_(zone) {}

  void set_zone(const ClockZone *zone) {
    if (zone != zone_) {
      zone_ = zone;
      day_ = -1;
    }
  }
  bool is_day(const std::optional<std::tm> &local);
  float night_warmth(const std::optional<std::tm> &local);
  std::optional<SolarTimes> times(const std::optional<std::tm> &local);

 private:
  bool update_(const std::optional<std::tm> &local);
  const ClockZone *zone_{};
  int year_{};
  int day_{-1};
  int rise_{};
  int set_{};
};

}  // namespace espframe::native
