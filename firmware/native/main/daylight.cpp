#include "daylight.h"

#include "solar_times.h"

#include <algorithm>

namespace espframe::native {
namespace {

int minute_of_day(const std::tm &time) { return time.tm_hour * 60 + time.tm_min; }

}  // namespace

bool Daylight::update_(const std::optional<std::tm> &local) {
  if (!zone_ || !local)
    return false;
  if (year_ != local->tm_year || day_ != local->tm_yday) {
    const auto offset = local_noon_offset(zone_, *local);
    if (!offset)
      return false;
    int rise_hour{}, rise_minute{}, set_hour{}, set_minute{};
    calc_sunrise_sunset(local->tm_year + 1900, local->tm_mon + 1, local->tm_mday,
                        zone_->latitude, zone_->longitude, *offset,
                        rise_hour, rise_minute, set_hour, set_minute);
    rise_ = rise_hour * 60 + rise_minute;
    set_ = set_hour * 60 + set_minute;
    year_ = local->tm_year;
    day_ = local->tm_yday;
  }
  return true;
}

bool Daylight::is_day(const std::optional<std::tm> &local) {
  if (!update_(local))
    return true;
  const int minute = minute_of_day(*local);
  if (rise_ <= set_)
    return minute >= rise_ && minute < set_;
  return minute >= rise_ || minute < set_;
}

std::optional<SolarTimes> Daylight::times(const std::optional<std::tm> &local) {
  if (!update_(local))
    return {};
  return SolarTimes{rise_, set_};
}

float Daylight::night_warmth(const std::optional<std::tm> &local) {
  if (!update_(local))
    return 0.0f;
  constexpr int minutes_per_day = 24 * 60;
  constexpr int ramp = 60;
  const int length = (set_ - rise_ + minutes_per_day) % minutes_per_day;
  const int after_rise = (minute_of_day(*local) - rise_ + minutes_per_day) % minutes_per_day;
  if (after_rise >= length)
    return 1.0f;
  const int before_set = length - after_rise;
  return 1.0f - static_cast<float>(std::min({after_rise, before_set, ramp})) / ramp;
}

}  // namespace espframe::native
