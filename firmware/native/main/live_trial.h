#pragma once

#include <cstdint>

namespace espframe::native {

struct Settings;

bool read_live_trial_credentials(Settings &settings);
bool start_live_trial_status();
void record_live_trial_photo(uint32_t elapsed_ms);
void record_live_trial_gesture(uint32_t motion_us);

}  // namespace espframe::native
