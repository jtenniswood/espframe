#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "configuration_contract_generated.h"
#include "ntp_helpers.h"

namespace esphome::espframe {

inline uint32_t schedule_wake_timeout_ms(float seconds) {
  if (std::isnan(seconds) || seconds <= 0) seconds = contract::SCHEDULE_WAKE_TIMEOUT_DEFAULT;
  return static_cast<uint32_t>(
      std::clamp(seconds, contract::SCHEDULE_WAKE_TIMEOUT_MIN, contract::SCHEDULE_WAKE_TIMEOUT_MAX) * 1000.0f);
}

inline bool validate_configuration_text(const char *key, const std::string &value) {
  if (std::strcmp(key, "ntp_server_1") == 0 ||
      std::strcmp(key, "ntp_server_2") == 0 ||
      std::strcmp(key, "ntp_server_3") == 0) {
    return espframe_is_valid_ntp_server(value);
  }
  return true;
}

inline bool is_portrait_rotation(const std::string &rotation) {
  return rotation == "90" || rotation == "270";
}

inline bool rotation_requires_developer_mode(const std::string &rotation, bool developer_enabled) {
  return !developer_enabled && is_portrait_rotation(rotation);
}

inline bool resolve_portrait_pairing(const std::string &rotation, bool requested) {
  return requested && !is_portrait_rotation(rotation);
}

// Apply developer mode before rotation, then evaluate explicit pairing
// against the resulting rotation. Other writes keep their relative order.
template<typename Setting>
void order_rotation_settings(std::vector<Setting> &settings, std::size_t developer,
                             std::size_t rotation, std::size_t pairing) {
  if (rotation == settings.size()) {
    // Disabling developer mode can itself reset a portrait rotation.
    if (pairing < developer && developer < settings.size()) {
      std::rotate(settings.begin() + pairing, settings.begin() + developer,
                  settings.begin() + developer + 1);
    }
    return;
  }
  if (rotation < developer && developer < settings.size()) {
    std::rotate(settings.begin() + rotation, settings.begin() + developer,
                settings.begin() + developer + 1);
    if (rotation <= pairing && pairing < developer) ++pairing;
    ++rotation;
  }
  if (pairing < rotation) {
    std::rotate(settings.begin() + pairing, settings.begin() + pairing + 1,
                settings.begin() + rotation + 1);
  }
}

}  // namespace esphome::espframe
