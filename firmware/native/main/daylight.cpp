#include "daylight.h"

#include "solar_times.h"

namespace espframe::native {
namespace {

int minute_of_day(const std::tm &time) { return time.tm_hour * 60 + time.tm_min; }

float offset_at_noon(std::tm local) {
  local.tm_hour = 12;
  local.tm_min = 0;
  local.tm_sec = 0;
  local.tm_isdst = -1;
  const auto epoch = std::mktime(&local);
  std::tm utc{};
  if (epoch == -1 || !gmtime_r(&epoch, &utc))
    return 0.0f;

  const int local_date = local.tm_year * 366 + local.tm_yday;
  const int utc_date = utc.tm_year * 366 + utc.tm_yday;
  const int day_shift = (local_date > utc_date) - (local_date < utc_date);
  return (minute_of_day(local) - minute_of_day(utc) + day_shift * 1440) / 60.0f;
}

}  // namespace

bool Daylight::is_day(const std::optional<std::tm> &local) {
  if (!zone_ || !local)
    return true;
  if (year_ != local->tm_year || day_ != local->tm_yday) {
    int rise_hour{}, rise_minute{}, set_hour{}, set_minute{};
    calc_sunrise_sunset(local->tm_year + 1900, local->tm_mon + 1, local->tm_mday,
                        zone_->latitude, zone_->longitude, offset_at_noon(*local),
                        rise_hour, rise_minute, set_hour, set_minute);
    rise_ = rise_hour * 60 + rise_minute;
    set_ = set_hour * 60 + set_minute;
    year_ = local->tm_year;
    day_ = local->tm_yday;
  }
  const int minute = minute_of_day(*local);
  if (rise_ <= set_)
    return minute >= rise_ && minute < set_;
  return minute >= rise_ || minute < set_;
}

}  // namespace espframe::native
