#pragma once

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace espframe::native {

inline std::optional<std::chrono::sys_days> parse_calendar_day(std::string_view date) {
  if (date.size() != 10 || date[4] != '-' || date[7] != '-')
    return {};
  const auto part = [&](unsigned first, unsigned last) {
    unsigned value{};
    const auto parsed = std::from_chars(date.data() + first, date.data() + last, value);
    return parsed.ec == std::errc{} && parsed.ptr == date.data() + last ? value : 0;
  };
  const auto y = part(0, 4);
  const std::chrono::year_month_day day{std::chrono::year(static_cast<int>(y)), std::chrono::month(part(5, 7)),
                                       std::chrono::day(part(8, 10))};
  if (!day.ok() || y < 2 || y > 9998)
    return {};
  return std::chrono::sys_days{day};
}

struct CaptureDateRange {
  std::optional<std::chrono::sys_days> from, through;
  bool active() const { return from.has_value() || through.has_value(); }
  bool includes(std::optional<std::chrono::sys_days> date) const {
    return !active() || (date && (!from || *date >= *from) && (!through || *date <= *through));
  }
  bool operator==(const CaptureDateRange &) const = default;
};

struct CaptureDateSettings {
  static constexpr std::string_view fixed_mode = "Fixed Range";
  static constexpr std::string_view relative_mode = "Relative Range";
  bool enabled{};
  bool relative{};
  std::string from;
  std::string through;
  uint8_t amount{1};
  bool years{true};

  const char *mode_name() const { return relative ? relative_mode.data() : fixed_mode.data(); }
  const char *unit_name() const { return years ? "Years" : "Months"; }
  bool needs_time() const { return enabled && relative; }

  std::optional<CaptureDateRange> resolve(std::optional<std::chrono::sys_days> today) const {
    if (!enabled)
      return CaptureDateRange{};
    if (!relative)
      return CaptureDateRange{parse_calendar_day(from), parse_calendar_day(through)};
    if (!today)
      return {};
    using namespace std::chrono;
    const year_month_day end{*today};
    const auto month = end.year() / end.month() - months{years ? amount * 12 : amount};
    const auto day = std::min(end.day(), (month / last).day());
    return CaptureDateRange{sys_days{month / day}, today};
  }
};

}  // namespace espframe::native
