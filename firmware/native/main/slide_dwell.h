#pragma once

#include <cstdint>

namespace espframe::native {

// Start the dwell when the settled photo is actually on screen. Keep the
// elapsed dwell while waiting for the next photo to finish preparing.
class SlideDwell {
 public:
  bool elapsed(int64_t now_us, bool visible_and_idle, int64_t duration_us) {
    if (!visible_and_idle) {
      reset();
      return false;
    }
    if (this->visible_since_us_ < 0)
      this->visible_since_us_ = now_us;
    return now_us - this->visible_since_us_ >= duration_us;
  }

  int progress_tenths_of_a_degree(int64_t now_us, int64_t duration_us) const {
    if (this->visible_since_us_ < 0 || duration_us <= 0)
      return 0;
    const auto visible_us = now_us - this->visible_since_us_;
    if (visible_us >= duration_us)
      return 3600;
    // Limit redraws of the idle frame to 20 Hz.
    constexpr int64_t sample_us = 50'000;
    return (visible_us / sample_us * sample_us) * 3600 / duration_us;
  }

  void reset() { this->visible_since_us_ = -1; }

 private:
  int64_t visible_since_us_{-1};
};

}  // namespace espframe::native
