#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

namespace espframe::native {

inline std::atomic<uint8_t> active_device_language{};

struct PhotoMetadata {
  std::optional<std::chrono::sys_days> capture_day;
  std::array<char, 65> city{};
  std::array<char, 65> country{};
  bool location_known{};
};

struct PhotoMetadataSettings {
  static constexpr std::string_view date_taken = "Date Taken";
  static constexpr std::string_view relative_date = "Relative Date";
  static constexpr std::string_view day_first = "1 January, 2026";
  static constexpr std::string_view month_first = "January 1, 2026";

  bool show_date{true};
  bool show_location{true};
  bool relative{};
  bool month_before_day{};
  uint8_t language{};

  bool needs_time() const { return show_date && relative; }
  const char *date_format() const { return relative ? relative_date.data() : date_taken.data(); }
  const char *taken_format() const { return month_before_day ? month_first.data() : day_first.data(); }
};

}  // namespace espframe::native
