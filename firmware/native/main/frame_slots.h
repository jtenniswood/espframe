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

  unsigned preparation_slot() const {
    for (unsigned index = 0; index < count_; ++index)
      if (index != front_ && index != pinned_ && index != pending_ && index != reserved_)
        return index;
    return none;
  }

  bool can_submit(unsigned index) const {
    return !queued() && index < count_ && index != front_ && index != pinned_ && index != reserved_;
  }

  unsigned writable() const { return queued() ? none : preparation_slot(); }

  // Idle zoom preparation borrows only an extra framebuffer. The usual
  // double buffering remains available for a swipe while work cancels.
  unsigned reserve_preparation() {
    if (!can_pin() || queued() || reserved_ != none || pinned_ != none)
      return none;
    reserved_ = preparation_slot();
    return reserved_;
  }

  bool release_preparation(unsigned index) {
    if (index == none || index != reserved_)
      return false;
    reserved_ = none;
    return true;
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
  unsigned reserved_{none};
};

}  // namespace espframe::native
