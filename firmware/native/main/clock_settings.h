#pragma once

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
};

struct ClockZone {
  const char *option;
  const char *label;
  const char *posix;
};

std::span<const ClockZone> clock_zones();
const char *clock_timezone(std::string_view option);
bool configure_local_time(std::string_view timezone);
std::optional<std::tm> local_clock_time(std::time_t now);

}  // namespace espframe::native
