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

inline std::optional<SlideIndex> missing_neighbor_at_edge(PixelPosition position, SlideRange ready, SlideRange available,
                                                         int width = slide_width) {
  if (position < ready.first * width && ready.first > available.first)
    return ready.first - 1;
  if (position > ready.last * width && ready.last < available.last)
    return ready.last + 1;
  return {};
}

class SlidePrefetch {
 public:
  explicit SlidePrefetch(SlideIndex center, size_t slots = SlideCache::minimum_capacity, int width = slide_width)
      : center_(center), slots_(static_cast<SlideIndex>(std::clamp(slots, SlideCache::minimum_capacity,
                                                                  SlideCache::capacity))), width_(width) {}

  void moved_to(SlideIndex center) {
    if (center == center_)
      return;
    direction_ = center > center_ ? 1 : -1;
    if (direction_ < 0)
      stage_forward_until_ = 0;
    center_ = center;
    requested_.reset();
  }

  void request_at_edge(PixelPosition position, SlideRange ready, SlideRange available) {
    requested_ = missing_neighbor_at_edge(position, ready, available, width_);
    if (requested_)
      direction_ = *requested_ < center_ ? -1 : 1;
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

  void note_drag(PixelPosition offset, int64_t now) {
    if (offset != 0)
      stage_forward_until_ = offset > 0 ? now + 2'000'000 : 0;
  }

  bool should_stage_forward(SlideIndex candidate, SlideRange ready, int64_t now) const {
    return center_ > 0 && candidate == ready.last + 1 && ready.last - center_ < desired_ready_in_direction &&
           (now < stage_forward_until_ || requested_ == candidate || promoted_ == candidate);
  }

  bool can_promote(SlideIndex candidate) const {
    return center_ > 0 && candidate > center_ && requested_ == candidate && promoted_ != candidate;
  }

  void promoted(SlideIndex candidate) { promoted_ = candidate; }

  void prepared(SlideIndex slide) {
    if (requested_ == slide)
      requested_.reset();
    if (promoted_ == slide)
      promoted_ = -1;
  }

  bool can_yield(SlideIndex active, SlideIndex wanted, bool refreshing = false) const {
    return active != wanted && (refreshing || active != center_);
  }

  bool can_defer_drag(SlideRange ready, SlideIndex origin, PixelPosition position, bool starting,
                      bool first_motion_pending = true) const {
    if (center_ == 0 || requested_ || !ready.contains(origin))
      return false;
    const auto anchor = origin * width_;
    // Follow the side of the photo under the finger. A touch can grab a
    // moving photo whose origin differs from the last settled slide.
    // After the opening frames, keep working if the landing will be the
    // last ready photo. The decoder is already behind the next gesture.
    // With only the forward landing ready, resume decoding as soon as the
    // first drag frame reaches the panel so the next swipe can also be ready.
    const auto before = starting ? 1 : desired_ready_in_direction;
    const auto after = starting && first_motion_pending ? 1 : desired_ready_in_direction;
    return (position <= anchor && origin - ready.first >= before) ||
           (position >= anchor && ready.last - origin >= after);
  }

  bool can_defer_settle(SlideRange ready, SlideIndex destination) const {
    const bool backward = destination == center_ ? direction_ < 0 : destination < center_;
    const auto prepared = backward ? destination - ready.first : ready.last - destination;
    return center_ > 0 && !requested_ && prepared >= desired_ready_in_direction;
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
  int width_;
  int direction_{1};
  int64_t stage_forward_until_{};
  std::optional<SlideIndex> requested_;
  SlideIndex promoted_{-1};
};

}  // namespace espframe::native
