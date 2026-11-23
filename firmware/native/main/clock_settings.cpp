#include "clock_settings.h"

#include <cstdlib>
#include <mutex>

namespace espframe::native {
namespace {

constexpr ClockZone zones[]{
#include "native_clock_zones.inc"
};

std::mutex timezone_mutex;
const ClockZone *configured_zone;
std::mutex ntp_mutex;
NtpServers configured_ntp_servers{"pool.ntp.org", "", ""};

}  // namespace

void publish_ntp_servers(const NtpServers &servers) {
  const std::lock_guard lock(ntp_mutex);
  if (servers != configured_ntp_servers) {
    configured_ntp_servers = servers;
    ntp_settings_revision.fetch_add(1, std::memory_order_release);
  }
}

std::optional<NtpServers> updated_ntp_servers(uint32_t &revision) {
  if (revision == ntp_settings_revision.load(std::memory_order_acquire))
    return {};
  const std::lock_guard lock(ntp_mutex);
  revision = ntp_settings_revision.load(std::memory_order_relaxed);
  return configured_ntp_servers;
}

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
  return configure_local_time(find_clock_zone(timezone));
}

bool configure_local_time(const ClockZone *zone) {
  if (!zone)
    return false;
  const std::lock_guard lock(timezone_mutex);
  if (zone == configured_zone)
    return true;
  if (setenv("TZ", zone->posix, 1) != 0)
    return false;
  tzset();
  configured_zone = zone;
  return true;
}

LocalClockReading local_clock_reading(std::time_t now) {
  const std::lock_guard lock(timezone_mutex);
  std::tm local{};
  if (!clock_time_is_valid(now) || !localtime_r(&now, &local))
    return {configured_zone, std::nullopt};
  return {configured_zone, local};
}

std::optional<std::tm> local_clock_time(std::time_t now) { return local_clock_reading(now).time; }

std::optional<float> local_noon_offset(const ClockZone *zone, std::tm local) {
  const std::lock_guard lock(timezone_mutex);
  if (zone != configured_zone)
    return {};
  local.tm_hour = 12;
  local.tm_min = local.tm_sec = 0;
  local.tm_isdst = -1;
  const auto epoch = std::mktime(&local);
  std::tm utc{};
  if (epoch == -1 || !gmtime_r(&epoch, &utc))
    return {};
  const int local_date = local.tm_year * 366 + local.tm_yday;
  const int utc_date = utc.tm_year * 366 + utc.tm_yday;
  const int day_shift = (local_date > utc_date) - (local_date < utc_date);
  return ((local.tm_hour - utc.tm_hour) * 60 + local.tm_min - utc.tm_min + day_shift * 1440) / 60.0f;
}

}  // namespace espframe::native
