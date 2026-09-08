#pragma once

#include <cstddef>

namespace esphome {
namespace espframe {

// Leave headroom above the 32 KiB internal reserve before speculative work.
// Hysteresis avoids repeatedly starting/stopping prefetch near the boundary.
// Foreground photo requests do not use this gate.
class MemoryPressurePolicy {
 public:
  bool allow_background_work(size_t free_bytes, size_t largest_block) {
    if (this->paused_) {
      this->paused_ = free_bytes < 64 * 1024 || largest_block < 24 * 1024;
    } else {
      this->paused_ = free_bytes < 48 * 1024 || largest_block < 16 * 1024;
    }
    return !this->paused_;
  }

 private:
  bool paused_ = false;
};

}  // namespace espframe
}  // namespace esphome
