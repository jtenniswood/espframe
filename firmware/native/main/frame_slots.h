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

  unsigned writable() const { return queued() ? none : spare(); }

  unsigned spare() const {
    for (unsigned index = 0; index < count_; ++index) {
      if (is_spare_(index))
        return index;
    }
    return none;
  }

  bool can_submit(unsigned index) const { return !queued() && is_spare_(index); }

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
  bool is_spare_(unsigned index) const {
    return index < count_ && index != front_ && index != pending_ && index != pinned_;
  }

  unsigned count_;
  unsigned front_{};
  unsigned pending_{none};
  unsigned pinned_{none};
};

}  // namespace espframe::native
