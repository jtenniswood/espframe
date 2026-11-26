#pragma once

#include "settings.h"
#include "stock_connection_preferences.h"

#include <cmath>
#include <cstdint>
#include <string_view>

namespace espframe::native {

struct TrialScope {
  uint32_t source{}, orientation{}, rotation{}, date_mode{}, date_unit{1}, pairing_range{};
  uint8_t date_enabled{}, pairs_only{}, pairing_enabled{1};
  float date_amount{1};
  std::string_view date_from, date_to;

  bool apply(Settings &settings) const {
    // The stock product uses preset indexes 0 and 6 for All Photos and
    // Custom. The caller checks the individual custom filter switches.
    if ((source != 0 && source != 6) || orientation > 2 || (rotation != 0 && rotation != 2) ||
        date_enabled > 1 || pairs_only > 1 || pairing_enabled > 1 || pairing_range >= portrait_pairing_ranges.size() ||
        date_mode > 1 || date_unit > 1 ||
        !std::isfinite(date_amount) || date_amount < 1 || date_amount > 120 || std::floor(date_amount) != date_amount)
      return false;
    if ((!date_from.empty() && !parse_calendar_day(date_from)) ||
        (!date_to.empty() && !parse_calendar_day(date_to)) ||
        (!date_from.empty() && !date_to.empty() && date_from > date_to))
      return false;
    settings.screen_rotation = rotation == 2 ? ScreenRotation::UpsideDown : ScreenRotation::Normal;
    constexpr PhotoOrientation stock_orientations[]{PhotoOrientation::Any, PhotoOrientation::Portrait, PhotoOrientation::Landscape};
    settings.photo_orientation = stock_orientations[orientation];
    settings.portrait_pairing = pairing_enabled != 0;
    settings.portrait_pairs_only = pairs_only != 0;
    settings.portrait_pairing_range_index = pairing_range;
    settings.capture_dates = {.enabled = date_enabled != 0, .relative = date_mode == 1,
                              .from = std::string(date_from), .through = std::string(date_to),
                              .amount = static_cast<uint8_t>(date_amount), .years = date_unit == 1};
    return true;
  }
};

}  // namespace espframe::native
