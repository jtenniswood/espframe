#pragma once

#include "clock_settings.h"

namespace espframe::native {

class Daylight {
 public:
  explicit Daylight(const ClockZone *zone) : zone_(zone) {}

  bool is_day(const std::optional<std::tm> &local);

 private:
  const ClockZone *zone_{};
  int year_{};
  int day_{-1};
  int rise_{};
  int set_{};
};

}  // namespace espframe::native
