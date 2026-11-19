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

struct SlideRetry {
  SlideIndex slide{};
  int64_t until{};
  uint32_t connection{};

  static SlideRetry after(SlideIndex slide, int64_t now, uint32_t connection, bool has_next_candidate) {
    return {slide, now + (has_next_candidate ? 100'000 : 2'000'000), connection};
  }

  bool due(SlideIndex candidate, int64_t now, uint32_t active_connection) const {
    return slide != candidate || now >= until || (active_connection != 0 && active_connection != connection);
  }
};

inline std::optional<SlideIndex> missing_neighbor_at_edge(PixelPosition position, SlideRange ready, SlideRange available) {
  if (position < ready.first * slide_width && ready.first > available.first)
    return ready.first - 1;
  if (position > ready.last * slide_width && ready.last < available.last)
    return ready.last + 1;
  return {};
}

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
    if (const auto missing = missing_neighbor_at_edge(position, ready, available)) {
      requested_ = missing;
      direction_ = *missing < ready.first ? -1 : 1;
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

  bool can_defer_pixel_work(SlideRange ready) const {
    return center_ > 0 && !requested_ && (ready.first < center_ || ready.last > center_);
  }

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
