#pragma once

#include "playback_trace.h"
#include "touch_report.h"

#include <algorithm>
#include <optional>

namespace espframe::native {

class TrialSwipeBenchmark {
 public:
  static constexpr unsigned swipe_limit = 48;
  static constexpr int64_t report_period_us = 8'000;
  static constexpr int64_t drag_us = 192'000;
  static constexpr int64_t rest_us = 420'000;

  struct Timing {
    uint32_t count{};
    uint32_t worst_us{};
    uint32_t over_limit{};

    void add(uint32_t elapsed_us, uint32_t limit_us) {
      ++count;
      worst_us = std::max(worst_us, elapsed_us);
      over_limit += elapsed_us > limit_us;
    }
  };

  struct Results {
    uint32_t started{};
    uint32_t settled{};
    uint32_t wrong_slide{};
    uint32_t cache_hits{};
    uint32_t dma_copies{};
    uint32_t cpu_copies{};
    uint32_t worst_composition_us{};
    uint32_t worst_before_submit_us{};
    uint32_t worst_submit_us{};
    Timing first_motion;
    Timing busy_first_motion;
    Timing drag_gaps;
    Timing busy_drag_gaps;
  };

  const Results &results() const { return results_; }

  std::optional<TouchReport> poll(int64_t now, SlideIndex center, SlideRange ready, bool at_rest) {
    if (dragging_) {
      if (now - last_sample_ < report_period_us)
        return {};
      const bool down = now - dragged_since_ < drag_us;
      dragging_ = down;
      waiting_for_settle_ = !down;
      const int64_t elapsed = std::min(now - dragged_since_, drag_us);
      return sample_(now, start_x_ - direction_ * static_cast<int>(elapsed * distance / drag_us), down);
    }
    if (waiting_for_settle_) {
      if (!at_rest)
        return {};
      ++results_.settled;
      results_.wrong_slide += center != destination_;
      waiting_for_settle_ = false;
      next_swipe_at_ = now + rest_us;
    }
    if (!at_rest || center < 1 || results_.started == swipe_limit || now < next_swipe_at_)
      return {};

    // Two forward swipes for every backward swipe. Travel beyond the first
    // ready window so the worker also prepares photos during the test.
    const int direction = results_.started % 3 == 1 ? -1 : 1;
    if (!ready.contains(center + direction))
      return {};
    direction_ = direction;
    destination_ = center + direction;
    start_x_ = direction > 0 ? 960 : 320;
    dragged_since_ = now;
    dragging_ = true;
    ++results_.started;
    return sample_(now, start_x_, true);
  }

  void gesture(uint32_t latency_us, const FirstMotionTrace &motion) {
    results_.first_motion.add(latency_us, 50'000);
    results_.worst_composition_us = std::max(results_.worst_composition_us, static_cast<uint32_t>(motion.composed_us));
    results_.worst_before_submit_us =
        std::max(results_.worst_before_submit_us, static_cast<uint32_t>(motion.composed_us - motion.submit_us));
    results_.worst_submit_us = std::max(results_.worst_submit_us, static_cast<uint32_t>(motion.submit_us));
    results_.cache_hits += motion.cache.result == CacheRead::Hit;
    if (motion.preparing)
      results_.busy_first_motion.add(latency_us, 50'000);
  }

  void blit(bool dma) { ++(dma ? results_.dma_copies : results_.cpu_copies); }

  void frame(int64_t now, MotionStamp stamp, uint32_t period_us) {
    if (stamp.phase == MotionPhase::Drag && previous_stamp_.phase == MotionPhase::Drag &&
        stamp.epoch == previous_stamp_.epoch) {
      const auto elapsed = static_cast<uint32_t>(now - previous_frame_us_);
      const auto gap_limit = period_us + period_us / 2;
      results_.drag_gaps.add(elapsed, gap_limit);
      if (stamp.preparing && previous_stamp_.preparing)
        results_.busy_drag_gaps.add(elapsed, gap_limit);
    }
    previous_frame_us_ = now;
    previous_stamp_ = stamp;
  }

 private:
  static constexpr int distance = 640;

  TouchReport sample_(int64_t now, int x, bool down) {
    last_sample_ = now;
    return {x, slide_height / 2, static_cast<uint8_t>(down), true, ++sequence_, now};
  }

  Results results_;
  SlideIndex destination_{};
  int64_t dragged_since_{};
  int64_t last_sample_{};
  int64_t next_swipe_at_{};
  int64_t previous_frame_us_{};
  MotionStamp previous_stamp_;
  uint32_t sequence_{};
  int start_x_{};
  int direction_{};
  bool dragging_{};
  bool waiting_for_settle_{};
};

}  // namespace espframe::native
