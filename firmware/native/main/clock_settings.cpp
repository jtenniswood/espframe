#include "clock_settings.h"

#include <cstdlib>

namespace espframe::native {
namespace {

constexpr ClockZone zones[]{
#include "native_clock_zones.inc"
};

}  // namespace

std::span<const ClockZone> clock_zones() { return zones; }

const ClockZone *find_clock_zone(std::string_view option) {
  for (const auto &zone : zones) {
    if (option == zone.option)
      return &zone;
  }
  return nullptr;
}

const char *clock_timezone(std::string_view option) {
  const auto *zone = find_clock_zone(option);
  return zone ? zone->posix : nullptr;
}

bool configure_local_time(std::string_view timezone) {
  const char *posix = clock_timezone(timezone);
  if (!posix || setenv("TZ", posix, 1) != 0)
    return false;
  tzset();
  return true;
}

std::optional<std::tm> local_clock_time(std::time_t now) {
  std::tm local{};
  if (!clock_time_is_valid(now) || !localtime_r(&now, &local))
    return {};
  return local;
}

}  // namespace espframe::native
