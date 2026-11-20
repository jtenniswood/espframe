#pragma once

#include <array>
#include <atomic>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace espframe::native {

struct ClockSettings {
  bool show{};
  std::string format{"24 Hour"};
  std::string timezone{"Europe/London (GMT+0)"};
  std::array<std::string, 3> ntp_servers{"pool.ntp.org", "", ""};

  bool twelve_hour() const { return format == "12 Hour"; }
};

inline std::atomic<bool> active_clock_twelve_hour{};

struct ClockZone {
  const char *option;
  const char *label;
  const char *posix;
  float latitude;
  float longitude;
};

std::span<const ClockZone> clock_zones();
const ClockZone *find_clock_zone(std::string_view option);
const char *clock_timezone(std::string_view option);
bool configure_local_time(std::string_view timezone);
std::optional<std::tm> local_clock_time(std::time_t now);
inline constexpr bool clock_time_is_valid(std::time_t now) { return now >= 1'690'000'000; }

}  // namespace espframe::native
