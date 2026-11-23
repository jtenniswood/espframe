#pragma once

#include <cmath>
#include <cstdio>
#include <string>

// ============================================================================
// NOAA sunrise/sunset calculator
// ============================================================================
// Simplified NOAA algorithm. Takes date, lat/lon, and UTC offset in hours.
// Writes sunrise and sunset as local hours and minutes.
// Returns false for polar day/night (no rise or set).

static constexpr float DEG_TO_RAD = M_PI / 180.0f;
static constexpr float RAD_TO_DEG = 180.0f / M_PI;

inline std::string format_time_12h(int h, int m) {
  char buf[16];
  const char *suffix = (h >= 12) ? "PM" : "AM";
  int dh = h % 12;
  if (dh == 0) dh = 12;
  std::snprintf(buf, sizeof(buf), "%d:%02d %s", dh, m, suffix);
  return buf;
}

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
