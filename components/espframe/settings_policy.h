#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace esphome::espframe {

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
