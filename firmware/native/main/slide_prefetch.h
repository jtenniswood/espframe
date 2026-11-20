#pragma once

#include "slide_geometry.h"
#include "slide_cache.h"

#include <algorithm>
#include <array>
#include <optional>

namespace espframe::native {

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
    requested_ = missing_neighbor_at_edge(position, ready, available);
    if (requested_ && *requested_ > center_)
      direction_ = 1;
  }

  void request_after_drag(std::optional<SlideIndex> waiting_history) {
    // Keep the first forward miss urgent through the settle and into idle.
    // Once it is ready, the normal window refills ahead in the same direction.
    if (waiting_history || (requested_ && *requested_ <= center_))
      requested_ = waiting_history;
  }

  SlideRange window(SlideRange available) const {
    const auto ideal_first = center_ - (refill_direction_() < 0 ? slots_ - 3 : 2);
    const auto rightmost_first = std::max(available.first, available.last - slots_ + 1);
    const auto first = std::clamp(ideal_first, available.first, rightmost_first);
    return {first, std::min(available.last, first + slots_ - 1)};
  }

  SlideRange nearby_window() const {
    if (refill_direction_() < 0)
      return {center_ - desired_ready_in_direction, center_ + 1};
    return {center_ - 1, center_ + desired_ready_in_direction};
  }

  template <class Eligible>
  bool has_nearby_refill(const SlideCache &cache, const Eligible &eligible) const {
    const auto [first, last] = nearby_window();
    for (auto slide = first; slide <= last; ++slide) {
      if (eligible(slide) && !cache.find(slide))
        return true;
    }
    return false;
  }

  std::optional<SlideIndex> requested() const { return requested_; }

  void prepared(SlideIndex slide) {
    if (requested_ == slide)
      requested_.reset();
  }

  bool can_yield(SlideIndex active, SlideIndex wanted, bool refreshing = false) const {
    return active != wanted && (refreshing || active != center_);
  }

  bool can_defer_drag(SlideRange ready, SlideIndex origin, PixelPosition position) const {
    if (center_ == 0 || requested_ || !ready.contains(origin))
      return false;
    const auto anchor = origin * slide_width;
    // Follow the side of the photo under the finger. A touch can grab a
    // moving photo whose origin differs from the last settled slide.
    return (position <= anchor && ready.first < origin) || (position >= anchor && origin < ready.last);
  }

  bool can_defer_pixel_work(SlideRange ready, bool contact_engaged, SlideIndex destination) const {
    const auto anchor = contact_engaged ? center_ : destination;
    const bool backward = anchor == center_ ? direction_ < 0 : anchor < center_;
    const auto prepared = backward ? anchor - ready.first : ready.last - anchor;
    // Protect the motion under the finger. During settle, count the photos
    // still available beyond the landing in the direction of the swipe.
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
    const int direction = refill_direction_();
    for (SlideIndex distance = 1; distance <= desired_ready_in_direction; ++distance)
      add(center_ + distance * direction);
    for (SlideIndex distance = 1; distance < slots_; ++distance) {
      if (distance > desired_ready_in_direction)
        add(center_ + distance * direction);
      add(center_ - distance * direction);
    }
    return result;
  }

 private:
  static constexpr SlideIndex desired_ready_in_direction = 2;

  int refill_direction_() const {
    return requested_ ? (*requested_ < center_ ? -1 : 1) : direction_;
  }

  SlideIndex center_;
  SlideIndex slots_;
  int direction_{1};
  std::optional<SlideIndex> requested_;
};

}  // namespace espframe::native
