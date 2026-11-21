#pragma once

#include "playback_trace.h"
#include "touch_report.h"

#include <algorithm>
#include <optional>

namespace espframe::native {

class TrialSwipeMetrics {
 public:
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
    uint32_t cache_hits{};
    uint32_t dma_copies{};
    uint32_t cpu_copies{};
    uint32_t worst_composition_us{};
    uint32_t worst_before_submit_us{};
    uint32_t worst_submit_us{};
    uint32_t split_blits{};
    uint32_t batched_split_blits{};
    uint32_t worst_split_blit_us{};
    uint64_t total_split_blit_us{};
    Timing first_motion;
    Timing cache_miss_motion;
    Timing busy_first_motion;
    Timing quiet_cache_hit_motion;
    Timing busy_cache_hit_motion;
    Timing drag_gaps;
    Timing quiet_drag_gaps;
    Timing busy_drag_gaps;
    Timing in_range_drag_gaps;
    Timing resisted_drag_gaps;
    Timing drag_handoff_delay;
    Timing in_range_drag_updates;
    Timing resisted_drag_updates;
    Timing settle_gaps;
    Timing in_range_settle_gaps;
    Timing resisted_settle_gaps;
    Timing landing_gaps;
    Timing approach_gaps;
  };

  const Results &results() const { return results_; }

  void gesture(uint32_t latency_us, const FirstMotionTrace &motion) {
    results_.first_motion.add(latency_us, 50'000);
    results_.worst_composition_us = std::max(results_.worst_composition_us, static_cast<uint32_t>(motion.composed_us));
    results_.worst_before_submit_us =
        std::max(results_.worst_before_submit_us, static_cast<uint32_t>(motion.composed_us - motion.submit_us));
    results_.worst_submit_us = std::max(results_.worst_submit_us, static_cast<uint32_t>(motion.submit_us));
    if (motion.cache.result == CacheRead::Hit) {
      ++results_.cache_hits;
      auto &cache_hit_motion = motion.preparing ? results_.busy_cache_hit_motion : results_.quiet_cache_hit_motion;
      cache_hit_motion.add(latency_us, 50'000);
    } else if (motion.cache.result == CacheRead::Miss) {
      results_.cache_miss_motion.add(latency_us, 50'000);
    }
    if (motion.preparing)
      results_.busy_first_motion.add(latency_us, 50'000);
  }

  void blit(bool dma) { ++(dma ? results_.dma_copies : results_.cpu_copies); }

  void split_blit(uint32_t elapsed_us, bool batched) {
    ++results_.split_blits;
    results_.batched_split_blits += batched;
    results_.total_split_blit_us += elapsed_us;
    results_.worst_split_blit_us = std::max(results_.worst_split_blit_us, elapsed_us);
  }

  void frame(int64_t now, MotionStamp stamp, uint32_t period_us, uint32_t handoff_delay_us, int64_t demand_us) {
    const auto gap_limit = period_us + period_us / 2;
    if (stamp.phase == MotionPhase::Drag) {
      results_.drag_handoff_delay.add(handoff_delay_us, 250);
      if (demand_us != 0) {
        auto &updates = stamp.resisted ? results_.resisted_drag_updates : results_.in_range_drag_updates;
        updates.add(static_cast<uint32_t>(now - demand_us), gap_limit);
      }
    }
    if (stamp.phase == MotionPhase::Drag && previous_stamp_.phase == MotionPhase::Drag &&
        stamp.epoch == previous_stamp_.epoch) {
      const auto elapsed = static_cast<uint32_t>(now - previous_frame_us_);
      results_.drag_gaps.add(elapsed, gap_limit);
      if (stamp.resisted == previous_stamp_.resisted) {
        auto &path_gaps = stamp.resisted ? results_.resisted_drag_gaps : results_.in_range_drag_gaps;
        path_gaps.add(elapsed, gap_limit);
      }
      if (stamp.preparing == previous_stamp_.preparing) {
        auto &workload_gaps = stamp.preparing ? results_.busy_drag_gaps : results_.quiet_drag_gaps;
        workload_gaps.add(elapsed, gap_limit);
      }
    }
    if (stamp.phase == MotionPhase::Settle && previous_stamp_.phase == MotionPhase::Settle &&
        stamp.epoch == previous_stamp_.epoch) {
      const auto elapsed = static_cast<uint32_t>(now - previous_frame_us_);
      results_.settle_gaps.add(elapsed, gap_limit);
      auto &stage_gaps = stamp.landing ? results_.landing_gaps : results_.approach_gaps;
      stage_gaps.add(elapsed, gap_limit);
      if (stamp.resisted == previous_stamp_.resisted) {
        auto &path_gaps = stamp.resisted ? results_.resisted_settle_gaps : results_.in_range_settle_gaps;
        path_gaps.add(elapsed, gap_limit);
      }
    }
    previous_frame_us_ = now;
    previous_stamp_ = stamp;
  }

 private:
  Results results_;
  int64_t previous_frame_us_{};
  MotionStamp previous_stamp_;
};

class TrialSwipeBenchmark {
 public:
  enum class Scenario { Normal, Rapid, History, WifiOutage };
  enum class WifiWindow { Waiting, Offline, Reconnected };

  static constexpr unsigned swipe_limit = 48;
  static constexpr unsigned history_forward_moves = 9;
  static constexpr unsigned wifi_offline_moves = 6;
  static constexpr unsigned wifi_reconnected_moves = 12;
  static constexpr int64_t report_period_us = 8'000;
  static constexpr int64_t drag_us = 192'000;
  static constexpr int64_t rest_us = 420'000;
  static constexpr int64_t rapid_rest_us = 50'000;
  static constexpr uint32_t refill_limit_us = 500'000;

  struct Results {
    uint32_t started{};
    uint32_t settled{};
    SlideIndex settled_slide{};
    uint32_t wrong_slide{};
    uint32_t cold_starts{};
    uint32_t cold_releases_unready{};
    uint32_t cold_release_advances{};
    uint32_t cold_returns{};
    uint32_t elapsed_us{};
    uint32_t offline_settled{};
    uint32_t reconnected_settled{};
    SlideIndex offline_ready_last{};
    TrialSwipeMetrics::Timing refill;
  };

  explicit TrialSwipeBenchmark(Scenario scenario = Scenario::Normal) : scenario_(scenario) {}

  const Results &results() const { return results_; }
  unsigned target() const {
    if (scenario_ == Scenario::WifiOutage)
      return wifi_offline_moves + wifi_reconnected_moves;
    return scenario_ == Scenario::History ? 2 * history_forward_moves : swipe_limit;
  }
  bool finished() const { return results_.settled == target(); }

  std::optional<TouchReport> poll(int64_t now, SlideIndex center, SlideRange ready, bool at_rest,
                                WifiWindow wifi = WifiWindow::Waiting) {
    if (finished())
      return {};
    if (scenario_ == Scenario::WifiOutage && wifi == WifiWindow::Offline)
      results_.offline_ready_last = std::max(results_.offline_ready_last, ready.last);
    if (missing_slide_ && ready.contains(*missing_slide_)) {
      results_.refill.add(static_cast<uint32_t>(now - missing_since_), refill_limit_us);
      missing_slide_.reset();
    }
    if (dragging_) {
      if (now - last_sample_ < report_period_us)
        return {};
      const bool down = now - dragged_since_ < drag_us;
      dragging_ = down;
      waiting_for_settle_ = !down;
      if (!down) {
        released_cold_ = cold_start_ && !ready.contains(destination_);
        results_.cold_releases_unready += released_cold_;
      }
      const int64_t elapsed = std::min(now - dragged_since_, drag_us);
      return sample_(now, start_x_ - direction_ * static_cast<int>(elapsed * distance / drag_us), down);
    }
    if (waiting_for_settle_) {
      if (!at_rest)
        return {};
      ++results_.settled;
      results_.settled_slide = center;
      results_.elapsed_us = static_cast<uint32_t>(now - started_at_);
      const bool returned = cold_start_ && center == origin_;
      results_.cold_returns += returned;
      results_.cold_release_advances += released_cold_ && center == destination_;
      // If a neighbor arrived during the drag, the movement remaining
      // after re-anchoring determines whether it advances or settles back.
      results_.wrong_slide += center != destination_ && !returned;
      if (scenario_ == Scenario::WifiOutage && center == destination_) {
        if (results_.started <= wifi_offline_moves && wifi == WifiWindow::Offline)
          ++results_.offline_settled;
        if (results_.started > wifi_offline_moves && wifi == WifiWindow::Reconnected)
          ++results_.reconnected_settled;
      }
      waiting_for_settle_ = false;
      next_swipe_at_ = now + (scenario_ == Scenario::Rapid ? rapid_rest_us : rest_us);
    }
    if (!at_rest || center < 1 || results_.started == target() || now < next_swipe_at_)
      return {};
    const bool offline_move = results_.started < wifi_offline_moves;
    if (scenario_ == Scenario::WifiOutage &&
        (offline_move ? wifi != WifiWindow::Offline
                      : wifi != WifiWindow::Reconnected || results_.offline_settled != wifi_offline_moves))
      return {};

    // The normal run waits for two forward moves and one backward move.
    // Rapid browsing mostly advances and reverses only toward a cached photo.
    // The history run reaches slide ten, then returns to slide one.
    const bool back = scenario_ == Scenario::History ? results_.started >= history_forward_moves
                    : scenario_ == Scenario::Rapid ? results_.started % 5 == 4 && ready.contains(center - 1)
                    : scenario_ == Scenario::WifiOutage ? offline_move && results_.started % 2 != 0
                                                   : results_.started % 3 == 1;
    const int direction = back ? -1 : 1;
    const auto destination = center + direction;
    const bool cold_start = !ready.contains(destination);
    if (scenario_ != Scenario::Rapid && cold_start)
      return {};
    origin_ = center;
    direction_ = direction;
    destination_ = destination;
    cold_start_ = cold_start;
    if (cold_start) {
      ++results_.cold_starts;
      if (missing_slide_ != destination) {
        missing_slide_ = destination;
        missing_since_ = now;
      }
    }
    start_x_ = direction > 0 ? 960 : 320;
    dragged_since_ = now;
    dragging_ = true;
    if (results_.started == 0)
      started_at_ = now;
    ++results_.started;
    return sample_(now, start_x_, true);
  }

 private:
  static constexpr int distance = 640;

  TouchReport sample_(int64_t now, int x, bool down) {
    last_sample_ = now;
    return {x, slide_height / 2, static_cast<uint8_t>(down), true, ++sequence_, now};
  }

  Results results_;
  std::optional<SlideIndex> missing_slide_;
  int64_t missing_since_{};
  SlideIndex origin_{};
  SlideIndex destination_{};
  int64_t dragged_since_{};
  int64_t last_sample_{};
  int64_t started_at_{};
  int64_t next_swipe_at_{};
  uint32_t sequence_{};
  int start_x_{};
  int direction_{};
  bool dragging_{};
  bool waiting_for_settle_{};
  bool cold_start_{};
  bool released_cold_{};
  Scenario scenario_;
};

}  // namespace espframe::native
