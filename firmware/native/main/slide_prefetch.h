#pragma once

#include "slide_geometry.h"

#include <algorithm>
#include <array>
#include <optional>

namespace espframe::native {

struct SlideRange {
  SlideIndex first;
  SlideIndex last;

  bool contains(SlideIndex slide) const { return slide >= first && slide <= last; }
};

class SlidePrefetch {
 public:
  explicit SlidePrefetch(SlideIndex center) : center_(center) {}

  void moved_to(SlideIndex center) {
    if (center == center_)
      return;
    direction_ = center > center_ ? 1 : -1;
    center_ = center;
    requested_.reset();
  }

  void request_at_edge(PixelPosition position, SlideRange ready, SlideRange available) {
    if (position < ready.first * slide_width && ready.first > available.first) {
      requested_ = ready.first - 1;
      direction_ = -1;
    } else if (position > ready.last * slide_width && ready.last < available.last) {
      requested_ = ready.last + 1;
      direction_ = 1;
    }
  }

  SlideRange window(SlideRange available) const {
    return {std::max(available.first, center_ - (direction_ < 0 ? 3 : 2)),
            std::min(available.last, center_ + (direction_ > 0 ? 3 : 2))};
  }

  std::optional<SlideIndex> requested() const { return requested_; }

  void prepared(SlideIndex slide) {
    if (requested_ == slide)
      requested_.reset();
  }

  bool can_yield(SlideIndex active, SlideIndex wanted) const { return active != center_ && active != wanted; }

  std::array<SlideIndex, 7> candidates() const {
    return {center_, requested_.value_or(-1), center_ + direction_, center_ - direction_,
            center_ + 2 * direction_, center_ - 2 * direction_, center_ + 3 * direction_};
  }

 private:
  SlideIndex center_;
  int direction_{1};
  std::optional<SlideIndex> requested_;
};

}  // namespace espframe::native
