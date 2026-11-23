#include "daylight.h"
#include "solar_times.h"

#include <cassert>
#include <cmath>

using namespace espframe::native;

void check_solar_boundaries(Daylight &daylight, const ClockZone &zone, int year, int month, int day, float offset) {
  int rise_h{}, rise_m{}, set_h{}, set_m{};
  assert(calc_sunrise_sunset(year, month, day, zone.latitude, zone.longitude, offset, rise_h, rise_m, set_h, set_m));
  assert(rise_h > 0 && set_h > rise_h);

  std::tm local{};
  local.tm_year = year - 1900;
  local.tm_mon = month - 1;
  local.tm_mday = day;
  local.tm_isdst = -1;
  assert(std::mktime(&local) != -1);
  const SolarTimes expected{rise_h * 60 + rise_m, set_h * 60 + set_m};
  assert(daylight.times(local) == expected);

  const auto is_day = [&](int minute) {
    local.tm_hour = minute / 60;
    local.tm_min = minute % 60;
    return daylight.is_day(local);
  };
  assert(!is_day(rise_h * 60 + rise_m - 1));
  assert(is_day(rise_h * 60 + rise_m));
  assert(is_day(set_h * 60 + set_m - 1));
  assert(!is_day(set_h * 60 + set_m));

  const int sunrise = rise_h * 60 + rise_m;
  const int sunset = set_h * 60 + set_m;
  const auto warmth = [&](int minute) {
    local.tm_hour = minute / 60;
    local.tm_min = minute % 60;
    return daylight.night_warmth(local);
  };
  assert(warmth(sunrise - 1) == 1.0f);
  assert(warmth(sunrise) == 1.0f);
  assert(std::abs(warmth(sunrise + 30) - 0.5f) < 0.001f);
  assert(warmth(sunrise + 60) == 0.0f);
  assert(warmth(sunset - 60) == 0.0f);
  assert(std::abs(warmth(sunset - 30) - 0.5f) < 0.001f);
  assert(warmth(sunset) == 1.0f);
  assert(warmth(sunset + 1) == 1.0f);
}

int main() {
  const auto *london = find_clock_zone("Europe/London (GMT+0)");
  assert(london && configure_local_time(london->option));
  Daylight london_solar{london};
  assert(!london_solar.times(local_clock_time(0)));
  assert(london_solar.is_day(local_clock_time(0)));
  assert(london_solar.night_warmth(local_clock_time(0)) == 0.0f);
  check_solar_boundaries(london_solar, *london, 2026, 7, 1, 1.0f);
  check_solar_boundaries(london_solar, *london, 2026, 1, 1, 0.0f);
  // The same local date in a different year must discard the previous solar times.
  check_solar_boundaries(london_solar, *london, 2027, 1, 1, 0.0f);

  const auto *kathmandu = find_clock_zone("Asia/Kathmandu (GMT+5:45)");
  assert(kathmandu && configure_local_time(kathmandu->option));
  const auto new_reading = local_clock_reading(1'782'909'240);
  assert(new_reading.zone == kathmandu && new_reading.time && new_reading.time->tm_min == 19);
  assert(!local_noon_offset(london, *new_reading.time));
  london_solar.set_zone(kathmandu);
  check_solar_boundaries(london_solar, *kathmandu, 2027, 1, 1, 5.75f);
  Daylight nepal_solar{kathmandu};
  check_solar_boundaries(nepal_solar, *kathmandu, 2026, 7, 1, 5.75f);

  const auto *kiritimati = find_clock_zone("Pacific/Kiritimati (GMT+14)");
  assert(kiritimati && configure_local_time(kiritimati->option));
  Daylight island_solar{kiritimati};
  // At local noon on 1 January UTC is still in the previous year.
  check_solar_boundaries(island_solar, *kiritimati, 2027, 1, 1, 14.0f);

  const ClockZone polar{"Test", "Test", "GMT0", 85.0f, 0.0f};
  assert(configure_local_time(&polar));
  Daylight polar_solar{&polar};
  std::tm midsummer{};
  midsummer.tm_mday = 21;
  midsummer.tm_mon = 5;
  midsummer.tm_year = 126;
  midsummer.tm_isdst = -1;
  assert(std::mktime(&midsummer) != -1);
  const SolarTimes fallback{6 * 60, 18 * 60};
  assert(polar_solar.times(midsummer) == fallback);
}
