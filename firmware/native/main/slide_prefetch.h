#pragma once

#include "slide_geometry.h"
#include "slide_cache.h"

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
  explicit SlidePrefetch(SlideIndex center, size_t slots = SlideCache::minimum_capacity)
      : center_(center), slots_(static_cast<SlideIndex>(std::clamp(slots, SlideCache::minimum_capacity,
                                                                  SlideCache::capacity))) {}

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
    const auto ideal_first = center_ - (direction_ < 0 ? slots_ - 3 : 2);
    const auto rightmost_first = std::max(available.first, available.last - slots_ + 1);
    const auto first = std::clamp(ideal_first, available.first, rightmost_first);
    return {first, std::min(available.last, first + slots_ - 1)};
  }

  std::optional<SlideIndex> requested() const { return requested_; }

  void prepared(SlideIndex slide) {
    if (requested_ == slide)
      requested_.reset();
  }

  bool can_yield(SlideIndex active, SlideIndex wanted) const { return active != center_ && active != wanted; }

  bool can_defer_pixel_work(SlideRange ready, bool contact_engaged) const {
    const auto prepared = direction_ < 0 ? center_ - ready.first : ready.last - center_;
    // Keep the drag under the finger responsive. When only the landing
    // slide is ready, refill during the settle so the next swipe has a chance
    // to find another photo without exhausting the cache.
    return center_ > 0 && !requested_ && prepared >= (contact_engaged ? 1 : desired_ready_in_direction);
  }

  std::array<SlideIndex, SlideCache::capacity + 2> candidates(SlideRange keep) const {
    std::array<SlideIndex, SlideCache::capacity + 2> result;
    result.fill(-1);
    result[0] = center_;
    result[1] = requested_.value_or(-1);
    size_t index = 2;
    const auto add = [&](SlideIndex slide) {
      if (keep.contains(slide))
        result[index++] = slide;
    };
    for (SlideIndex distance = 1; distance <= desired_ready_in_direction; ++distance)
      add(center_ + distance * direction_);
    for (SlideIndex distance = 1; distance < slots_; ++distance) {
      if (distance > desired_ready_in_direction)
        add(center_ + distance * direction_);
      add(center_ - distance * direction_);
    }
    return result;
  }

 private:
  static constexpr SlideIndex desired_ready_in_direction = 2;

  SlideIndex center_;
  SlideIndex slots_;
  int direction_{1};
  std::optional<SlideIndex> requested_;
};

}  // namespace espframe::native
