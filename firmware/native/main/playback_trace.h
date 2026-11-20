#pragma once

#include "slide_prefetch.h"

namespace espframe::native {

enum class MotionPhase : char { Still = '-', Drag = 'd', Settle = 's', Crossfade = 'f' };
enum class CacheRead : char { Hit = 'h', Miss = 'm', Edge = 'e' };
enum class RefillOutcome : char { Ready = 'r', Retry = 'e', Cancelled = 'c' };

struct MotionStamp {
  uint32_t epoch{};
  MotionPhase phase{MotionPhase::Still};
  bool preparing{};
  bool resisted{};
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

struct CacheSample {
  CacheRead result;
  SlideIndex missing_slide{-1};
};

inline CacheSample ready_at_motion(PixelPosition position, SlideRange ready, SlideRange available) {
  if (const auto missing = missing_neighbor_at_edge(position, ready, available))
    return {CacheRead::Miss, *missing};
  if (position < ready.first * slide_width || position > ready.last * slide_width)
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
