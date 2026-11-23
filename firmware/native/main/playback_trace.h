#pragma once

#include "slide_prefetch.h"

#include <utility>

namespace espframe::native {

enum class MotionPhase : char { Still = '-', Drag = 'd', Settle = 's', Crossfade = 'f' };
enum class CacheRead : char { Hit = 'h', Miss = 'm', Edge = 'e' };
enum class RefillOutcome : char { Ready = 'r', Retry = 'e', Cancelled = 'c' };

struct MotionStamp {
  uint32_t epoch{};
  MotionPhase phase{MotionPhase::Still};
  bool preparing{};
  bool resisted{};
  bool landing{};
};

class MotionEpoch {
 public:
  MotionPhase phase() const { return phase_; }
  void update(MotionPhase phase) {
    if (phase != phase_) {
      phase_ = phase;
      ++epoch_;
    }
  }
  MotionStamp stamp(bool preparing, bool resisted = false) const { return {epoch_, phase_, preparing, resisted}; }

 private:
  uint32_t epoch_{};
  MotionPhase phase_{MotionPhase::Still};
};

class DragFrameDemand {
 public:
  void observe(int64_t now, bool dragging, PixelPosition wanted, PixelPosition submitted) {
    if (!dragging || wanted == submitted)
      oldest_us_ = 0;
    else if (oldest_us_ == 0)
      oldest_us_ = now;
  }

  int64_t take() { return std::exchange(oldest_us_, 0); }

 private:
  int64_t oldest_us_{};
};

struct CacheSample {
  CacheRead result;
  SlideIndex missing_slide{-1};
};

inline CacheSample ready_at_motion(PixelPosition position, SlideRange ready, SlideRange available, int width = slide_width) {
  if (const auto missing = missing_neighbor_at_edge(position, ready, available, width))
    return {CacheRead::Miss, *missing};
  if (position < ready.first * width || position > ready.last * width)
    return {CacheRead::Edge};
  return {CacheRead::Hit};
}

struct FirstMotionTrace {
  int64_t sampled_us{};
  int64_t contact_us{};
  int64_t composed_us{};
  CacheSample cache{CacheRead::Edge};
  bool preparing{};
  int64_t submit_us{};
};

}  // namespace espframe::native
