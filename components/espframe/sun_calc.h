#pragma once
#include <string>
#include <cstdint>
#include <cmath>
#include <ctime>

#ifdef USE_TIME_TIMEZONE
#include "esphome/components/time/posix_tz.h"
#endif
#include "esphome/core/version.h"
#include "solar_times.h"

// ============================================================================
// Timezone data lookup table — generated from timezones.py via __init__.py
// ============================================================================

struct TzInfo { const char* tz; float lat; float lon; const char* posix; };

#include "tz_data_generated.h"

inline bool lookup_tz_coords(const std::string &tz_id, float &lat, float &lon) {
  // Timezone IDs double as a rough location hint for sunrise/sunset when the
  // user has not supplied separate coordinates.
  for (int i = 0; i < TZ_DATA_COUNT; i++) {
    if (tz_id == TZ_DATA[i].tz) {
      lat = TZ_DATA[i].lat;
      lon = TZ_DATA[i].lon;
      return true;
    }
  }
  return false;
}

inline const char* lookup_tz_posix(const std::string &tz_id) {
  for (int i = 0; i < TZ_DATA_COUNT; i++) {
    if (tz_id == TZ_DATA[i].tz) {
      return TZ_DATA[i].posix;
    }
  }
  return nullptr;
}

#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 9, 0) && defined(USE_TIME_TIMEZONE)
inline const esphome::time::ParsedTimezone* lookup_tz_timezone(const std::string &tz_id) {
  for (int i = 0; i < TZ_DATA_COUNT; i++) {
    if (tz_id == TZ_DATA[i].tz) {
      return &TZ_TIMEZONE_DATA[i];
    }
  }
  return nullptr;
}
#endif

namespace esphome::espframe {
template<typename Clock>
inline bool set_sntp_timezone(const std::string &tz_id, const char *posix, Clock &clock) {
  if (posix == nullptr) return false;
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 9, 0)
#ifdef USE_TIME_TIMEZONE
  (void) clock;
  const auto* timezone = lookup_tz_timezone(tz_id);
  if (timezone != nullptr) {
    esphome::time::set_global_tz(*timezone);
    return true;
  }
#endif
  return false;
#else
  (void) tz_id;
  clock->set_timezone(posix);
  return true;
#endif
}
}  // namespace esphome::espframe

inline float active_tz_offset_hours(time_t epoch, float fallback_offset) {
#ifdef USE_TIME_TIMEZONE
  const auto &tz = esphome::time::get_global_tz();
  int32_t offset_seconds = esphome::time::is_in_dst(epoch, tz)
                             ? tz.dst_offset_seconds
                             : tz.std_offset_seconds;
  return -offset_seconds / 3600.0f;
#else
  return fallback_offset;
#endif
}

inline float parse_tz_offset(const std::string &tz_label) {
  // The UI exposes timezone labels like "Europe/London (GMT+0)". This extracts
  // the GMT part for the sunrise/sunset calculator.
  auto pos = tz_label.find("GMT");
  if (pos == std::string::npos) return 0.0f;
  std::string offset_str = tz_label.substr(pos + 3);
  if (offset_str.empty() || offset_str == "+0" || offset_str == "0") return 0.0f;
  float sign = 1.0f;
  size_t idx = 0;
  if (offset_str[idx] == '+') { sign = 1.0f; idx++; }
  else if (offset_str[idx] == '-') { sign = -1.0f; idx++; }
  auto paren = offset_str.find(')');
  if (paren != std::string::npos) offset_str = offset_str.substr(0, paren);
  char *end = nullptr;
  auto colon = offset_str.find(':', idx);
  if (colon != std::string::npos) {
    float hours = strtof(offset_str.c_str() + idx, &end);
    if (end == offset_str.c_str() + idx) return 0.0f;
    float mins = strtof(offset_str.c_str() + colon + 1, &end);
    if (end == offset_str.c_str() + colon + 1) return 0.0f;
    return sign * (hours + mins / 60.0f);
  }
  float val = strtof(offset_str.c_str() + idx, &end);
  if (end == offset_str.c_str() + idx) return 0.0f;
  return sign * val;
}
