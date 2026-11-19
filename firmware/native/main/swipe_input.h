#pragma once

#include "swipe.h"
#include "touch_report.h"

#include <cstdint>

namespace espframe::native {

class SwipeInput {
 public:
  bool engaged() const { return down_ || wait_for_release_; }

  void update(Swipe &swipe, const TouchReport &report, PixelPosition handoff) {
    const bool lost_samples = seen_report_ && report.sequence != sequence_ + uint32_t{1};
    seen_report_ = true;
    sequence_ = report.sequence;
    if (lost_samples)
      cancel_(swipe, report.sampled_us);

    if (!report.valid) {
      if (down_ && ++errors_ == 3)
        cancel_(swipe, report.sampled_us);
      return;
    }
    errors_ = 0;
    if (report.contacts > 1) {
      cancel_(swipe, report.sampled_us);
      return;
    }
    if (report.contacts == 0) {
      if (down_) {
        swipe.release(report.sampled_us);
        if (swipe.idle() && swipe.position() == handoff)
          first_motion_us_ = 0;
      }
      down_ = false;
      wait_for_release_ = false;
      return;
    }
    if (wait_for_release_)
      return;

    if (!down_) {
      swipe.press(report.x, report.y, handoff, report.sampled_us);
      first_motion_us_ = 0;
      down_ = true;
    }
    const bool was_dragging = swipe.dragging();
    swipe.move(report.x, report.y, report.sampled_us);
    if (!was_dragging && swipe.dragging() && swipe.position() != handoff)
      first_motion_us_ = report.sampled_us;
  }

  int64_t take_first_motion() {
    const auto result = first_motion_us_;
    first_motion_us_ = 0;
    return result;
  }

 private:
  void cancel_(Swipe &swipe, int64_t now_us) {
    swipe.cancel(now_us);
    down_ = false;
    wait_for_release_ = true;
    first_motion_us_ = 0;
    errors_ = 0;
  }

  bool seen_report_{};
  bool down_{};
  bool wait_for_release_{};
  unsigned errors_{};
  uint32_t sequence_{};
  int64_t first_motion_us_{};
};

}  // namespace espframe::native
