#pragma once

#include <cstdint>

namespace espframe::native {

// Start a one-boot swipe demo while the ESPHome app is still the rollback target.
bool start_esphome_trial(uint32_t seconds = 180);
void end_esphome_trial();

}  // namespace espframe::native
