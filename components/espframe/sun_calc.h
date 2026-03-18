#pragma once
#include <string>
#include <cstdint>
#include <cmath>

// ============================================================================
// Timezone coordinate lookup table
// ============================================================================
// Representative city lat/lon for each timezone in the selection list.
// Used to calculate sunrise/sunset times on-device (no API dependency).

struct TzCoord { const char* tz; float lat; float lon; };

static const TzCoord TZ_COORDS[] = {
  {"Pacific/Midway",                    28.21f, -177.38f},
  {"Pacific/Pago_Pago",               -14.27f, -170.70f},
  {"Pacific/Honolulu",                  21.31f, -157.86f},
  {"America/Adak",                      51.88f, -176.66f},
  {"America/Anchorage",                 61.22f, -149.90f},
  {"America/Juneau",                    58.30f, -134.42f},
  {"America/Los_Angeles",               34.05f, -118.24f},
  {"America/Vancouver",                 49.28f, -123.12f},
  {"America/Tijuana",                   32.51f, -117.04f},
  {"America/Denver",                    39.74f, -104.98f},
  {"America/Phoenix",                  33.45f, -112.07f},
  {"America/Edmonton",                  53.55f, -113.49f},
  {"America/Boise",                     43.62f, -116.21f},
  {"America/Chicago",                   41.88f,  -87.63f},
  {"America/Mexico_City",               19.43f,  -99.13f},
  {"America/Winnipeg",                  49.90f,  -97.14f},
  {"America/Guatemala",                 14.63f,  -90.51f},
  {"America/Costa_Rica",                 9.93f,  -84.08f},
  {"America/New_York",                  40.71f,  -74.01f},
  {"America/Toronto",                   43.65f,  -79.38f},
  {"America/Detroit",                   42.33f,  -83.05f},
  {"America/Havana",                    23.11f,  -82.37f},
  {"America/Bogota",                     4.71f,  -74.07f},
  {"America/Lima",                     -12.05f,  -77.04f},
  {"America/Jamaica",                   18.11f,  -77.30f},
  {"America/Panama",                     8.98f,  -79.52f},
  {"America/Halifax",                   44.65f,  -63.57f},
  {"America/Caracas",                   10.49f,  -66.88f},
  {"America/Santiago",                 -33.45f,  -70.67f},
  {"America/La_Paz",                   -16.50f,  -68.15f},
  {"America/Manaus",                    -3.12f,  -60.02f},
  {"America/Barbados",                  13.10f,  -59.61f},
  {"America/Puerto_Rico",              18.47f,  -66.11f},
  {"America/Santo_Domingo",            18.49f,  -69.93f},
  {"America/St_Johns",                  47.56f,  -52.71f},
  {"America/Sao_Paulo",               -23.55f,  -46.63f},
  {"America/Argentina/Buenos_Aires",   -34.60f,  -58.38f},
  {"America/Montevideo",              -34.88f,  -56.16f},
  {"America/Paramaribo",                5.85f,  -55.17f},
  {"Atlantic/South_Georgia",          -54.28f,  -36.51f},
  {"Atlantic/Azores",                  38.72f,  -27.22f},
  {"Atlantic/Cape_Verde",              14.93f,  -23.51f},
  {"UTC",                               51.51f,   -0.13f},
  {"Europe/London",                     51.51f,   -0.13f},
  {"Europe/Dublin",                     53.35f,   -6.26f},
  {"Europe/Lisbon",                     38.72f,   -9.14f},
  {"Africa/Casablanca",                 33.57f,   -7.59f},
  {"Africa/Accra",                       5.56f,   -0.19f},
  {"Atlantic/Reykjavik",               64.15f,  -21.94f},
  {"Europe/Paris",                      48.86f,    2.35f},
  {"Europe/Berlin",                     52.52f,   13.40f},
  {"Europe/Rome",                       41.90f,   12.50f},
  {"Europe/Madrid",                     40.42f,   -3.70f},
  {"Europe/Amsterdam",                  52.37f,    4.90f},
  {"Europe/Brussels",                   50.85f,    4.35f},
  {"Europe/Vienna",                     48.21f,   16.37f},
  {"Europe/Zurich",                     47.38f,    8.54f},
  {"Europe/Stockholm",                  59.33f,   18.07f},
  {"Europe/Oslo",                       59.91f,   10.75f},
  {"Europe/Copenhagen",                 55.68f,   12.57f},
  {"Europe/Warsaw",                     52.23f,   21.01f},
  {"Europe/Prague",                     50.08f,   14.44f},
  {"Europe/Budapest",                   47.50f,   19.04f},
  {"Europe/Belgrade",                   44.79f,   20.47f},
  {"Africa/Lagos",                       6.45f,    3.39f},
  {"Africa/Tunis",                      36.81f,   10.17f},
  {"Africa/Cairo",                      30.04f,   31.24f},
  {"Europe/Athens",                     37.98f,   23.73f},
  {"Europe/Bucharest",                  44.43f,   26.10f},
  {"Europe/Helsinki",                   60.17f,   24.94f},
  {"Europe/Kyiv",                       50.45f,   30.52f},
  {"Europe/Istanbul",                   41.01f,   28.98f},
  {"Africa/Johannesburg",             -26.20f,   28.05f},
  {"Africa/Nairobi",                    -1.29f,   36.82f},
  {"Asia/Jerusalem",                    31.77f,   35.22f},
  {"Asia/Amman",                        31.95f,   35.93f},
  {"Asia/Beirut",                       33.89f,   35.50f},
  {"Europe/Moscow",                     55.76f,   37.62f},
  {"Asia/Baghdad",                      33.31f,   44.37f},
  {"Asia/Riyadh",                       24.69f,   46.72f},
  {"Asia/Kuwait",                       29.38f,   47.98f},
  {"Asia/Qatar",                        25.29f,   51.53f},
  {"Africa/Addis_Ababa",                 9.01f,   38.75f},
  {"Asia/Tehran",                       35.69f,   51.39f},
  {"Asia/Dubai",                        25.20f,   55.27f},
  {"Asia/Muscat",                       23.59f,   58.54f},
  {"Asia/Baku",                         40.41f,   49.87f},
  {"Asia/Tbilisi",                      41.72f,   44.79f},
  {"Indian/Mauritius",                 -20.16f,   57.50f},
  {"Asia/Kabul",                        34.53f,   69.17f},
  {"Asia/Karachi",                      24.86f,   67.01f},
  {"Asia/Tashkent",                     41.30f,   69.28f},
  {"Asia/Yekaterinburg",                56.84f,   60.60f},
  {"Asia/Kolkata",                      28.61f,   77.21f},
  {"Asia/Colombo",                       6.93f,   79.84f},
  {"Asia/Kathmandu",                    27.72f,   85.32f},
  {"Asia/Dhaka",                        23.81f,   90.41f},
  {"Asia/Almaty",                       43.24f,   76.95f},
  {"Asia/Rangoon",                      16.87f,   96.20f},
  {"Asia/Bangkok",                      13.76f,  100.50f},
  {"Asia/Jakarta",                      -6.21f,  106.85f},
  {"Asia/Ho_Chi_Minh",                  10.82f,  106.63f},
  {"Asia/Singapore",                     1.35f,  103.82f},
  {"Asia/Kuala_Lumpur",                  3.14f,  101.69f},
  {"Asia/Shanghai",                     31.23f,  121.47f},
  {"Asia/Hong_Kong",                    22.32f,  114.17f},
  {"Asia/Taipei",                       25.03f,  121.57f},
  {"Asia/Manila",                       14.60f,  120.98f},
  {"Australia/Perth",                  -31.95f,  115.86f},
  {"Asia/Tokyo",                        35.68f,  139.69f},
  {"Asia/Seoul",                        37.57f,  126.98f},
  {"Asia/Pyongyang",                    39.02f,  125.75f},
  {"Australia/Adelaide",               -34.93f,  138.60f},
  {"Australia/Darwin",                 -12.46f,  130.84f},
  {"Australia/Sydney",                 -33.87f,  151.21f},
  {"Australia/Melbourne",              -37.81f,  144.96f},
  {"Australia/Brisbane",               -27.47f,  153.03f},
  {"Australia/Hobart",                 -42.88f,  147.33f},
  {"Pacific/Guam",                      13.44f,  144.79f},
  {"Pacific/Port_Moresby",             -6.31f,  147.17f},
  {"Asia/Vladivostok",                  43.12f,  131.91f},
  {"Pacific/Noumea",                   -22.28f,  166.46f},
  {"Pacific/Norfolk",                  -29.05f,  167.96f},
  {"Asia/Magadan",                      59.56f,  150.80f},
  {"Pacific/Auckland",                 -36.85f,  174.76f},
  {"Pacific/Fiji",                     -18.14f,  178.44f},
  {"Pacific/Chatham",                  -43.88f, -176.46f},
  {"Pacific/Tongatapu",               -21.21f, -175.15f},
  {"Pacific/Apia",                     -13.83f, -171.76f},
  {"Pacific/Kiritimati",                 1.87f, -157.47f},
};

static constexpr int TZ_COORDS_COUNT = sizeof(TZ_COORDS) / sizeof(TZ_COORDS[0]);

inline bool lookup_tz_coords(const std::string &tz_id, float &lat, float &lon) {
  for (int i = 0; i < TZ_COORDS_COUNT; i++) {
    if (tz_id == TZ_COORDS[i].tz) {
      lat = TZ_COORDS[i].lat;
      lon = TZ_COORDS[i].lon;
      return true;
    }
  }
  return false;
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
  auto pos = tz_label.find("GMT");
  if (pos == std::string::npos) return 0.0f;
  std::string offset_str = tz_label.substr(pos + 3);
  if (offset_str.empty() || offset_str == "+0" || offset_str == "0") return 0.0f;
  float sign = 1.0f;
  size_t idx = 0;
  if (offset_str[idx] == '+') { sign = 1.0f; idx++; }
  else if (offset_str[idx] == '-') { sign = -1.0f; idx++; }
  // Strip trailing ')'
  auto paren = offset_str.find(')');
  if (paren != std::string::npos) offset_str = offset_str.substr(0, paren);
  auto colon = offset_str.find(':', idx);
  if (colon != std::string::npos) {
    float hours = std::stof(offset_str.substr(idx, colon - idx));
    float mins = std::stof(offset_str.substr(colon + 1));
    return sign * (hours + mins / 60.0f);
  }
  return sign * std::stof(offset_str.substr(idx));
}
