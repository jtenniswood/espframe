#pragma once
#include <string>
#include <cstdint>
#include <cmath>

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

// ============================================================================
// NOAA sunrise/sunset calculator
// ============================================================================
// Simplified NOAA algorithm. Takes date, lat/lon, and UTC offset in hours.
// Writes sunrise and sunset as local hours and minutes.
// Returns false for polar day/night (no rise or set).

static constexpr float DEG_TO_RAD = M_PI / 180.0f;
static constexpr float RAD_TO_DEG = 180.0f / M_PI;

inline bool calc_sunrise_sunset(int year, int month, int day,
                                float lat, float lon, float tz_offset,
                                int &rise_h, int &rise_m,
                                int &set_h, int &set_m) {
  int n1 = 275 * month / 9;
  int n2 = (month + 9) / 12;
  int n3 = 1 + ((year - 4 * (year / 4) + 2) / 3);
  int day_of_year = n1 - (n2 * n3) + day - 30;

  float lng_hour = lon / 15.0f;

  auto calc_time = [&](bool is_sunrise, int &out_h, int &out_m) -> bool {
    float t = is_sunrise
      ? day_of_year + ((6.0f - lng_hour) / 24.0f)
      : day_of_year + ((18.0f - lng_hour) / 24.0f);

    float mean_anomaly = (0.9856f * t) - 3.289f;
    float sun_lon = mean_anomaly
      + (1.916f * sinf(mean_anomaly * DEG_TO_RAD))
      + (0.020f * sinf(2.0f * mean_anomaly * DEG_TO_RAD))
      + 282.634f;
    while (sun_lon < 0) sun_lon += 360.0f;
    while (sun_lon >= 360.0f) sun_lon -= 360.0f;

    float ra = RAD_TO_DEG * atanf(0.91764f * tanf(sun_lon * DEG_TO_RAD));
    while (ra < 0) ra += 360.0f;
    while (ra >= 360.0f) ra -= 360.0f;

    int l_quad = ((int)(sun_lon / 90.0f)) * 90;
    int ra_quad = ((int)(ra / 90.0f)) * 90;
    ra += (l_quad - ra_quad);
    ra /= 15.0f;

    float sin_dec = 0.39782f * sinf(sun_lon * DEG_TO_RAD);
    float cos_dec = cosf(asinf(sin_dec));

    float zenith = 90.833f;
    float cos_h = (cosf(zenith * DEG_TO_RAD) - (sin_dec * sinf(lat * DEG_TO_RAD)))
                  / (cos_dec * cosf(lat * DEG_TO_RAD));

    if (cos_h > 1.0f || cos_h < -1.0f) return false;

    float h;
    if (is_sunrise)
      h = 360.0f - RAD_TO_DEG * acosf(cos_h);
    else
      h = RAD_TO_DEG * acosf(cos_h);
    h /= 15.0f;

    float local_t = h + ra - (0.06571f * t) - 6.622f;
    float ut = local_t - lng_hour;
    while (ut < 0) ut += 24.0f;
    while (ut >= 24.0f) ut -= 24.0f;

    float local_time = ut + tz_offset;
    while (local_time < 0) local_time += 24.0f;
    while (local_time >= 24.0f) local_time -= 24.0f;

    out_h = (int)local_time;
    out_m = (int)((local_time - out_h) * 60.0f);
    return true;
  };

  bool ok_rise = calc_time(true, rise_h, rise_m);
  bool ok_set = calc_time(false, set_h, set_m);

  if (!ok_rise) { rise_h = 6; rise_m = 0; }
  if (!ok_set)  { set_h = 18; set_m = 0; }

  return ok_rise && ok_set;
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
