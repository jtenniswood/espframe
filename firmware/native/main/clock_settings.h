#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace espframe::native {

using NtpServers = std::array<std::string, 3>;

struct ClockSettings {
  bool show{};
  std::string format{"24 Hour"};
  std::string timezone{"Europe/London (GMT+0)"};
  NtpServers ntp_servers{"pool.ntp.org", "", ""};

  bool twelve_hour() const { return format == "12 Hour"; }
};

inline std::atomic<bool> active_clock_twelve_hour{};
inline std::atomic<bool> active_clock_visible{};

struct ClockZone {
  const char *option;
  const char *label;
  const char *posix;
  float latitude;
  float longitude;
};

struct LocalClockReading {
  const ClockZone *zone;
  std::optional<std::tm> time;
};

inline std::atomic<const ClockZone *> requested_clock_zone{};
inline std::atomic<uint32_t> ntp_settings_revision{};

void publish_ntp_servers(const NtpServers &servers);
std::optional<NtpServers> updated_ntp_servers(uint32_t &revision);
std::span<const ClockZone> clock_zones();
const ClockZone *find_clock_zone(std::string_view option);
const char *clock_timezone(std::string_view option);
bool configure_local_time(std::string_view timezone);
bool configure_local_time(const ClockZone *zone);
LocalClockReading local_clock_reading(std::time_t now);
std::optional<std::tm> local_clock_time(std::time_t now);
std::optional<float> local_noon_offset(const ClockZone *zone, std::tm local);
inline constexpr bool clock_time_is_valid(std::time_t now) { return now >= 1'690'000'000; }

}  // namespace espframe::native
