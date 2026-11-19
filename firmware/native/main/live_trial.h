#pragma once

#include <cstddef>
#include <cstdint>

#if SWIPE_TRIAL_BENCHMARK
#include "trial_swipe_benchmark.h"
#endif

namespace espframe::native {

struct Settings;
struct FirstMotionTrace;
struct MotionStamp;
class NativeNetwork;

bool read_live_trial_credentials(Settings &settings);
bool start_live_trial_status(NativeNetwork &network);
void record_live_trial_wifi_link(int power_save, int rssi, unsigned channel);
void record_live_trial_prepare();
void record_live_trial_candidate_retry();
void record_live_trial_pixel_pause(uint32_t pause_ms);
void record_live_trial_http_open_error(int error);
void record_live_trial_http_read_timeout(bool headers);
void record_live_trial_search(bool pairing, int http_status, size_t candidates);
void record_live_trial_preview(int http_status);
void record_live_trial_photo(uint32_t elapsed_ms);
void record_live_trial_gesture(uint32_t motion_us, const FirstMotionTrace &motion);
void record_live_trial_frame(int64_t latched_us, MotionStamp stamp, uint32_t period_us);
void record_live_trial_blit(bool dma);

#if SWIPE_TRIAL_BENCHMARK
std::optional<TouchReport> poll_live_trial_benchmark(int64_t now, SlideIndex center, SlideRange ready, bool at_rest);
#endif

}  // namespace espframe::native
