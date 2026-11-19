#pragma once

#include "wifi_credentials.h"
#include "settings.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace espframe::native {

// Stock Espframe V1 builds with Wi-Fi configured through the frame's
// installer use the ESPHome fallback station preference.
inline constexpr const char *esphome_station_key = "88491487";
// ESPHome 2026.11 template text preference for Connection: Server URL,
// and restoring string preference for immich_api_key_value, respectively.
inline constexpr const char *esphome_immich_url_key = "2632273216";
inline constexpr const char *esphome_immich_api_key = "1991976547";
inline constexpr size_t esphome_station_bytes = 33 + 65;
inline constexpr size_t esphome_url_bytes = 256;
inline constexpr size_t esphome_api_key_bytes = 255;

inline std::optional<WifiCredentials> read_trial_station(std::span<const uint8_t> payload) {
  if (payload.size() != esphome_station_bytes)
    return {};
  const auto *text = reinterpret_cast<const char *>(payload.data());
  const std::string_view ssid(text, 33);
  const std::string_view password(text + 33, 65);
  const auto ssid_end = ssid.find('\0');
  const auto password_end = password.find('\0');
  if (ssid_end == ssid.npos || password_end == password.npos)
    return {};
  return WifiCredentials::from(ssid.substr(0, ssid_end), password.substr(0, password_end));
}

inline std::string_view read_trial_preference(std::span<const uint8_t> payload) {
  if (payload.empty() || payload[0] >= payload.size())
    return {};
  const std::string_view text(reinterpret_cast<const char *>(payload.data() + 1), payload[0]);
  return text.find('\0') == text.npos ? text : std::string_view{};
}

struct TrialScope {
  uint32_t source{}, orientation{}, rotation{}, date_mode{}, date_unit{1};
  uint8_t date_enabled{}, pairs_only{};
  float date_amount{1};
  std::string_view date_from, date_to;

  bool apply(Settings &settings) const {
    // The stock product uses preset indexes 0 and 6 for All Photos and
    // Custom. The caller checks the individual custom filter switches.
    if ((source != 0 && source != 6) || orientation != 0 || (rotation != 0 && rotation != 2) ||
        date_enabled > 1 || pairs_only > 1 || date_mode > 1 || date_unit > 1 ||
        !std::isfinite(date_amount) || date_amount < 1 || date_amount > 120 || std::floor(date_amount) != date_amount)
      return false;
    if ((!date_from.empty() && !parse_calendar_day(date_from)) ||
        (!date_to.empty() && !parse_calendar_day(date_to)) ||
        (!date_from.empty() && !date_to.empty() && date_from > date_to))
      return false;
    settings.screen_rotation = rotation == 2 ? ScreenRotation::UpsideDown : ScreenRotation::Normal;
    settings.portrait_pairs_only = pairs_only != 0;
    settings.capture_dates = {.enabled = date_enabled != 0, .relative = date_mode == 1,
                              .from = std::string(date_from), .through = std::string(date_to),
                              .amount = static_cast<uint8_t>(date_amount), .years = date_unit == 1};
    return true;
  }
};

}  // namespace espframe::native
