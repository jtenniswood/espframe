#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "configuration_contract_generated.h"

namespace esphome::espframe {

inline uint32_t schedule_wake_timeout_ms(float seconds) {
  if (std::isnan(seconds) || seconds <= 0) seconds = contract::SCHEDULE_WAKE_TIMEOUT_DEFAULT;
  return static_cast<uint32_t>(
      std::clamp(seconds, contract::SCHEDULE_WAKE_TIMEOUT_MIN, contract::SCHEDULE_WAKE_TIMEOUT_MAX) * 1000.0f);
}

inline bool resolve_portrait_pairing(const std::string &rotation, bool requested) {
  return requested && rotation != "90" && rotation != "270";
}

// Apply explicit pairing against the new rotation while keeping the other
// setting writes in their original relative order.
template<typename Setting>
void order_rotation_and_pairing(std::vector<Setting> &settings, std::size_t rotation, std::size_t pairing) {
  if (pairing < rotation && rotation < settings.size()) {
    std::rotate(settings.begin() + pairing, settings.begin() + pairing + 1,
                settings.begin() + rotation + 1);
  }
}

}  // namespace esphome::espframe
