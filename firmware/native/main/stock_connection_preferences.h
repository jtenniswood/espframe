#pragma once

#include "wifi_credentials.h"

namespace espframe::native {

// ESPHome 2026.11 preferences for Wi-Fi set through the installer, the
// Connection: Server URL entity, and the restoring Immich API key string.
inline constexpr const char *esphome_station_key = "88491487";
inline constexpr const char *esphome_immich_url_key = "2632273216";
inline constexpr const char *esphome_immich_api_key = "1991976547";
inline constexpr size_t esphome_station_bytes = 33 + 65;
inline constexpr size_t esphome_url_bytes = 256;
inline constexpr size_t esphome_api_key_bytes = 255;

inline std::optional<WifiCredentials> read_stock_station(std::span<const uint8_t> payload) {
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

inline std::string_view read_stock_preference_text(std::span<const uint8_t> payload) {
  if (payload.empty() || payload[0] >= payload.size())
    return {};
  const std::string_view text(reinterpret_cast<const char *>(payload.data() + 1), payload[0]);
  return text.find('\0') == text.npos ? text : std::string_view{};
}

}  // namespace espframe::native
