#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace espframe::native {

inline constexpr std::array<uint32_t, 18> stock_slideshow_intervals{
    10, 15, 20, 30, 45, 60, 120, 180, 300, 600, 900, 1800, 3600, 7200, 14'400, 28'800, 57'600, 86'400,
};

inline std::atomic<uint32_t> active_slideshow_interval{15};
inline constexpr std::array<uint32_t, 10> connection_timeouts{30, 45, 60, 120, 180, 300, 600, 900, 1200, 1800};
inline constexpr uint32_t default_connection_timeout = 600;
inline std::atomic<uint32_t> active_connection_timeout{default_connection_timeout};

inline bool valid_connection_timeout(uint32_t seconds) {
  return std::ranges::find(connection_timeouts, seconds) != connection_timeouts.end();
}

inline std::string interval_label(unsigned seconds) {
  if (seconds % 3600 == 0)
    return std::to_string(seconds / 3600) + (seconds == 3600 ? " hour" : " hours");
  if (seconds % 60 == 0)
    return std::to_string(seconds / 60) + (seconds == 60 ? " minute" : " minutes");
  return std::to_string(seconds) + " seconds";
}

inline unsigned interval_seconds(std::string_view value) {
  unsigned amount{};
  const auto end = value.data() + value.size();
  const auto result = std::from_chars(value.data(), end, amount);
  if (result.ec != std::errc{} || result.ptr == end || *result.ptr != ' ')
    return 0;
  auto unit = std::string_view(result.ptr + 1, end - result.ptr - 1);
  if (unit.ends_with('s'))
    unit.remove_suffix(1);
  const unsigned multiplier = unit == "second" ? 1 : unit == "minute" ? 60 : unit == "hour" ? 3600 : 0;
  return multiplier && amount <= 86'400 / multiplier ? amount * multiplier : 0;
}

}  // namespace espframe::native
