#pragma once

#include "swipe.h"
#include "touch_report.h"

#include <cstdint>
#include <cstdlib>

namespace espframe::native {

class SwipeInput {
 public:
  enum class Effect { None, Sleep, Wake, Tap };
  enum class DragEnd { None, Released, Cancelled };

  bool engaged() const { return down_ || wait_for_release_; }
  bool awake() const { return !sleeping_; }
  bool first_motion_pending() const { return first_motion_us_ != 0; }
  int64_t contact_started_us() const { return down_since_us_; }
  int contact_y() const { return down_y_; }

  void wait_for_new_contact() { wait_for_release_ = true; }

  void sleep(Swipe &swipe) {
    swipe.show(swipe.settled());
    wait_for_release_ |= down_;
    down_ = false;
    hold_candidate_ = false;
    first_motion_us_ = 0;
    sleeping_ = true;
  }

  void wake() { sleeping_ = false; }

  Effect update(Swipe &swipe, const TouchReport &report, PixelPosition handoff, DragEnd *drag_end = nullptr) {
    if (drag_end)
      *drag_end = DragEnd::None;
    const bool lost_samples = seen_report_ && report.sequence != sequence_ + uint32_t{1};
    seen_report_ = true;
    sequence_ = report.sequence;
    if (lost_samples && !sleeping_)
      cancel_(swipe, report.sampled_us, drag_end);

    if (!report.valid) {
      hold_candidate_ = false;
      if (down_ && ++errors_ == 3)
        cancel_(swipe, report.sampled_us, drag_end);
      return Effect::None;
    }
    errors_ = 0;
    if (report.contacts > 1) {
      cancel_(swipe, report.sampled_us, drag_end);
      return Effect::None;
    }
    if (report.contacts == 0) {
      const auto duration = report.sampled_us - down_since_us_;
      const bool sleep = down_ && hold_candidate_ && duration >= hold_duration_us;
      const bool tap = down_ && hold_candidate_ && tap_candidate_ && duration <= tap_duration_us;
      if (sleep) {
        this->sleep(swipe);
      } else if (down_) {
        if (drag_end && swipe.dragging())
          *drag_end = DragEnd::Released;
        swipe.release(report.sampled_us);
        if (swipe.idle() && swipe.position() == handoff)
          first_motion_us_ = 0;
      }
      down_ = false;
      hold_candidate_ = false;
      wait_for_release_ = false;
      if (sleep)
        return Effect::Sleep;
      return tap ? Effect::Tap : Effect::None;
    }
    if (wait_for_release_)
      return Effect::None;
    if (sleeping_) {
      // Consume the whole wake contact, including any drag before release.
      wake();
      wait_for_release_ = true;
      return Effect::Wake;
    }
    if (!down_) {
      tap_candidate_ = swipe.idle() && swipe.position() == handoff;
      swipe.press(report.x, report.y, handoff, report.sampled_us);
      first_motion_us_ = 0;
      down_ = true;
      hold_candidate_ = true;
      down_since_us_ = report.sampled_us;
      down_x_ = report.x;
      down_y_ = report.y;
    }
    const bool was_dragging = swipe.dragging();
    swipe.move(report.x, report.y, report.sampled_us);
    if (swipe.dragging() || std::abs(report.x - down_x_) > hold_slop || std::abs(report.y - down_y_) > hold_slop)
      hold_candidate_ = false;
    if (!was_dragging && swipe.dragging() && swipe.position() != handoff)
      first_motion_us_ = report.sampled_us;
    return Effect::None;
  }

  int64_t take_first_motion() {
    const auto result = first_motion_us_;
    first_motion_us_ = 0;
    return result;
  }

 private:
  static constexpr int64_t hold_duration_us = 3'000'000;
  static constexpr int64_t tap_duration_us = 280'000;
  static constexpr int hold_slop = 12;

  void cancel_(Swipe &swipe, int64_t now_us, DragEnd *drag_end) {
    if (drag_end && swipe.dragging())
      *drag_end = DragEnd::Cancelled;
    swipe.cancel(now_us);
    down_ = false;
    hold_candidate_ = false;
    wait_for_release_ = true;
    first_motion_us_ = 0;
    errors_ = 0;
  }

  bool seen_report_{};
  bool down_{};
  bool wait_for_release_{};
  bool sleeping_{};
  bool hold_candidate_{};
  bool tap_candidate_{};
  unsigned errors_{};
  uint32_t sequence_{};
  int64_t first_motion_us_{};
  int64_t down_since_us_{};
  int down_x_{};
  int down_y_{};
};

}  // namespace espframe::native
