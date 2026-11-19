#pragma once

namespace espframe::native {

// The renderer may keep one handed-off frame as a swipe source while the
// other two rotate through writing and scanout.
class FrameSlots {
 public:
  static constexpr unsigned none = 3;

  explicit FrameSlots(unsigned count = 2) : count_(count) {}
  bool can_pin() const { return count_ == 3; }
  bool queued() const { return pending_ != none; }

  unsigned writable() const {
    if (!queued()) {
      for (unsigned index = 0; index < count_; ++index) {
        if (index != front_ && index != pinned_)
          return index;
      }
    }
    return none;
  }

  void submit(unsigned index) { pending_ = index; }

  void latch() {
    front_ = pending_;
    pending_ = none;
  }

  unsigned pin() {
    if (!can_pin())
      return none;
    pinned_ = queued() ? pending_ : front_;
    return pinned_;
  }

  void unpin() { pinned_ = none; }

 private:
  unsigned count_;
  unsigned front_{};
  unsigned pending_{none};
  unsigned pinned_{none};
};

}  // namespace espframe::native
