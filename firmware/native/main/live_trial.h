#pragma once

#include <cstddef>
#include <cstdint>

namespace espframe::native {

struct Settings;
class NativeNetwork;

bool read_live_trial_credentials(Settings &settings);
bool start_live_trial_status(NativeNetwork &network);
void record_live_trial_prepare();
void record_live_trial_candidate_retry();
void record_live_trial_search(bool pairing, int http_status, size_t candidates);
void record_live_trial_preview(int http_status);
void record_live_trial_photo(uint32_t elapsed_ms);
void record_live_trial_gesture(uint32_t motion_us);

}  // namespace espframe::native
