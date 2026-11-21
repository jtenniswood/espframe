#pragma once

#include "slide_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace espframe::native {

class Swipe {
 public:
  enum class Transition { Quick, Glide };
  enum class DragActivity { Still, Starting, Moving };
  Swipe(int width, SlideIndex slide_count, SlideIndex initial_slide)
      : width_(width), ready_last_(slide_count - 1), settled_(initial_slide), target_(initial_slide) {}

  PixelPosition position() const {
    return this->settled_ * this->width_ + static_cast<PixelPosition>(std::floor(this->offset_ + 0.5f));
  }
  SlideIndex settled() const { return this->settled_; }
  SlideIndex origin_slide() const { return this->origin_slide_; }
  bool idle() const { return this->phase_ == Phase::Idle; }
  bool dragging() const { return this->phase_ == Phase::Dragging; }
  DragActivity drag_activity(int64_t now_us, int64_t moving_window_us, int64_t start_guard_us) const {
    if (!dragging() || now_us - this->last_motion_us_ >= moving_window_us)
      return DragActivity::Still;
    return now_us - this->drag_started_us_ < start_guard_us ? DragActivity::Starting : DragActivity::Moving;
  }
  bool settling() const { return this->phase_ == Phase::Settling; }
  SlideIndex settle_destination() const { return this->settling() ? this->target_ : this->settled_; }
  SlideRange retain_for_motion(SlideRange keep, PixelPosition presented) const {
    const auto include_slide = [&](SlideIndex slide) {
      keep.include(std::clamp(slide, this->ready_first_, this->ready_last_));
    };
    const auto include_position = [&](PixelPosition position) {
      const auto bounded = std::clamp(position, this->ready_first_ * this->width_, this->ready_last_ * this->width_);
      const auto first = bounded / this->width_;
      keep.include(first);
      if (bounded % this->width_ != 0)
        keep.include(first + 1);
    };

    // Retaking a moving photo uses the last presented position, which may
    // lag the current gesture. Keep the photos on both sides of that point.
    include_position(presented);
    if (this->idle())
      return keep;
    include_position(this->position());
    if (this->phase_ == Phase::Tracking || this->phase_ == Phase::Dragging || this->phase_ == Phase::Ignored) {
      include_slide(this->origin_slide_ - 1);
      include_slide(this->origin_slide_ + 1);
      include_slide(this->resume_);
    } else {
      include_slide(this->target_);
    }
    return keep;
  }

  void set_ready_range(SlideIndex first, SlideIndex last) {
    if (this->phase_ == Phase::Dragging &&
        ((first < this->ready_first_ && this->offset_ < this->slide_offset_(this->ready_first_)) ||
         (last > this->ready_last_ && this->offset_ > this->slide_offset_(this->ready_last_)))) {
      // A neighbor can finish downloading while the finger is at a resisted
      // edge. Continue from what's visible, without jumping to the raw drag.
      if (!this->release_offset_)
        this->release_offset_ = this->unresisted_offset_();
      this->origin_offset_ = this->offset_;
      this->origin_x_ = this->last_x_;
    }
    this->ready_first_ = first;
    this->ready_last_ = last;
  }

  void press(int x, int y, PixelPosition presented_position, int64_t now_us) {
    this->resume_ = this->target_;
    this->resume_transition_ = this->transition_;
    this->offset_ = static_cast<float>(presented_position - this->settled_ * this->width_);
    this->origin_offset_ = this->offset_;
    // Use the original rounding rule at a slide boundary, including when a
    // touch takes over the last frame of an animation in the other direction.
    const auto nearest = (presented_position + this->width_ / 2) / this->width_;
    this->origin_slide_ = std::clamp(nearest, this->ready_first_, this->ready_last_);
    // A pulled edge still displays only one photo. Use the normal release
    // threshold there, even when the handoff is between integer positions.
    this->grabbed_between_slides_ = presented_position % this->width_ != 0 &&
                                   presented_position > this->ready_first_ * this->width_ &&
                                   presented_position < this->ready_last_ * this->width_;
    this->press_x_ = x;
    this->origin_x_ = x;
    this->origin_y_ = y;
    this->previous_tracking_dx_ = 0;
    this->last_x_ = x;
    this->last_motion_us_ = now_us;
    this->velocity_ = 0;
    this->can_flick_ = false;
    this->release_offset_.reset();
    this->phase_ = Phase::Tracking;
  }

  void move(int x, int y, int64_t now_us) {
    if (this->phase_ == Phase::Tracking) {
      const int horizontal = x - this->origin_x_;
      const int dx = std::abs(horizontal);
      const int dy = std::abs(y - this->origin_y_);
      // A small vertical adjustment can precede a horizontal swipe. Lock
      // out the contact once the vertical movement is unambiguous.
      if (dy >= 32 && dy > dx) {
        this->phase_ = Phase::Ignored;
        return;
      }
      // Let sustained horizontal motion start sooner. A single small jump
      // during a stationary hold is still touch jitter.
      const int previous = this->previous_tracking_dx_;
      const int previous_distance = std::abs(previous);
      this->previous_tracking_dx_ = horizontal;
      const bool straight_start = dx >= straight_drag_distance && dx >= 2 * dy &&
                                  previous_distance >= straight_drag_distance / 2 && dx > previous_distance &&
                                  horizontal * previous > 0;
      const bool ordinary_start = dx >= flick_distance && dx * 2 >= dy * 3;
      if (!straight_start && !ordinary_start) {
        // Keep the velocity baseline current during a stationary hold,
        // including up to two pixels of reported horizontal jitter.
        if (dx <= 2) {
          this->last_x_ = x;
          this->last_motion_us_ = now_us;
        }
        return;
      }
      this->phase_ = Phase::Dragging;
      this->drag_started_us_ = now_us;
    }
    if (this->phase_ != Phase::Dragging)
      return;

    this->can_flick_ |= std::abs(x - this->press_x_) >= flick_distance;
    if (this->last_x_ != x) {
      const int64_t elapsed = now_us - this->last_motion_us_;
      const int delta = this->last_x_ - x;
      const float velocity = static_cast<float>(delta) * 1000.0f / std::max<int64_t>(elapsed, 1'000);
      this->velocity_ = elapsed < 85'000 ? 0.5f * (this->velocity_ + velocity) : 0.0f;
      this->last_motion_us_ = now_us;
      this->last_x_ = x;
      if (this->release_offset_)
        *this->release_offset_ += delta;
    }
    const auto first = this->minimum_slide_();
    const auto last = this->maximum_slide_();
    const float lower = this->slide_offset_(first);
    const float upper = this->slide_offset_(last);
    const float drawn = this->unresisted_offset_();
    if (drawn < lower && first == this->ready_first_) {
      this->offset_ = this->resisted_position_(drawn, lower);
    } else if (drawn > upper && last == this->ready_last_) {
      this->offset_ = this->resisted_position_(drawn, upper);
    } else {
      this->offset_ = std::clamp(drawn, lower, upper);
    }
    // Returning through the starting photo cancels the earlier edge pull.
    if (this->release_offset_) {
      const auto center = this->slide_offset_(this->origin_slide_);
      if ((this->offset_ - center) * (*this->release_offset_ - center) <= 0.0f)
        this->release_offset_.reset();
    }
  }

  void release(int64_t now_us) {
    if (this->phase_ == Phase::Tracking || this->phase_ == Phase::Ignored) {
      this->settle_to_(this->resume_, now_us, this->resume_transition_);
      return;
    }
    if (this->phase_ != Phase::Dragging)
      return;

    // A small early drag can move the picture without gaining flick momentum.
    const float velocity = this->can_flick_ && now_us - this->last_motion_us_ < 85'000
                               ? std::clamp(this->velocity_, -3.0f, 3.0f)
                               : 0.0f;
    const float momentum = velocity * 170.0f;
    const float projected = this->offset_ + momentum;
    // Match the stock gesture distance from rest. When taking over between
    // slides, keep enough margin to reverse the animation back by one photo.
    const float threshold = this->grabbed_between_slides_ ? this->width_ * 0.30f : 120.0f;
    const auto center = this->slide_offset_(this->origin_slide_);
    const auto direction = [center, threshold](float offset) {
      return offset - center >= threshold ? 1 : offset - center <= -threshold ? -1 : 0;
    };
    // Keep the original drag distance when a ready neighbor rebased the
    // visible drag and the release still points toward the same edge.
    const float raw_projection = this->release_offset_.value_or(this->unresisted_offset_()) + momentum;
    const bool track_original = this->release_offset_ && (projected - center) * (raw_projection - center) > 0.0f;
    const auto destination = std::clamp(this->origin_slide_ + direction(track_original ? raw_projection : projected),
                                        this->minimum_slide_(), this->maximum_slide_());
    // The release commits to a ready slide. A later refill can be used by
    // another gesture while this one finishes its snap-back.
    this->settle_to_(destination, now_us, Transition::Quick);
  }

  void cancel(int64_t now_us) {
    if (this->phase_ == Phase::Dragging || this->phase_ == Phase::Tracking ||
        this->phase_ == Phase::Ignored)
      this->settle_to_(this->resume_, now_us, this->resume_transition_);
  }

  bool advance(int64_t now_us, Transition transition, int64_t glide_duration_us = default_glide_duration_us) {
    if (this->phase_ != Phase::Idle || this->settled_ >= this->ready_last_)
      return false;
    if (transition == Transition::Glide)
      this->glide_duration_us_ = static_cast<float>(std::max<int64_t>(1, glide_duration_us));
    this->settle_to_(this->settled_ + 1, now_us, transition);
    return true;
  }

  void show(SlideIndex slide) {
    this->target_ = slide;
    this->finish_settle_();
  }

  void tick(int64_t now_us) {
    if (this->phase_ != Phase::Settling)
      return;

    const float t = std::clamp(static_cast<float>(now_us - this->settle_us_) / this->duration_us_,
                               0.0f, 1.0f);
    const float end = this->slide_offset_(this->target_);
    if (this->transition_ == Transition::Glide) {
      // Symmetric easing makes idle playback start and end at rest.
      const float progress = t * t * (3.0f - 2.0f * t);
      this->offset_ = this->settle_from_ + (end - this->settle_from_) * progress;
    } else {
      const float inverse = 1.0f - t;
      this->offset_ = end + (this->settle_from_ - end) * inverse * inverse * inverse;
    }
    if (t == 1.0f)
      this->finish_settle_();
  }

 private:
  enum class Phase { Idle, Tracking, Ignored, Dragging, Settling };
  static constexpr int64_t default_glide_duration_us = 640'000;
  static constexpr int straight_drag_distance = 8;
  static constexpr int flick_distance = 16;

  // Animation arithmetic stays near zero even after millions of photos.
  float slide_offset_(SlideIndex slide) const { return static_cast<float>((slide - this->settled_) * this->width_); }
  float unresisted_offset_() const { return this->origin_offset_ + (this->origin_x_ - this->last_x_); }

  float resisted_position_(float position, float edge) const {
    const float pull = position - edge;
    const float visible_pull = this->origin_offset_ - edge;
    const float anchor = pull * visible_pull > 0.0f ? visible_pull : 0.0f;
    // Returning from a grabbed edge follows the finger at full speed. Only
    // further movement beyond the visible anchor uses the remaining pull.
    if (std::abs(pull) <= std::abs(anchor))
      return position;
    const float limit = this->width_ * 0.025f;
    const float travel = pull - anchor;
    const float remaining = std::max(0.0f, limit - std::abs(anchor));
    return edge + anchor + travel * remaining / (std::abs(travel) + limit * 3.0f);
  }

  SlideIndex minimum_slide_() const { return std::max(this->ready_first_, this->origin_slide_ - 1); }
  SlideIndex maximum_slide_() const { return std::min(this->ready_last_, this->origin_slide_ + 1); }

  void finish_settle_() {
    this->settled_ = this->target_;
    this->offset_ = 0;
    this->transition_ = Transition::Quick;
    this->phase_ = Phase::Idle;
  }

  void settle_to_(SlideIndex slide, int64_t now_us, Transition transition) {
    this->target_ = slide;
    this->settle_from_ = this->offset_;
    this->settle_us_ = now_us;
    const float distance = std::abs(this->offset_ - this->slide_offset_(slide));
    if (distance < 0.5f) {
      this->finish_settle_();
      return;
    }
    const float fraction = std::min(distance / this->width_, 1.0f);
    this->transition_ = transition;
    const float minimum_glide = std::min(180'000.0f, this->glide_duration_us_);
    this->duration_us_ = transition == Transition::Glide
                             ? std::max(minimum_glide, this->glide_duration_us_ * fraction)
                             : 120'000.0f + 100'000.0f * fraction;
    this->phase_ = Phase::Settling;
  }

  int width_;
  SlideIndex ready_first_{};
  SlideIndex ready_last_;
  float offset_{};
  SlideIndex settled_;
  SlideIndex target_;
  Phase phase_{Phase::Idle};
  Transition transition_{Transition::Quick};
  Transition resume_transition_{Transition::Quick};
  SlideIndex resume_{};
  SlideIndex origin_slide_{};
  int press_x_{};
  int origin_x_{};
  int origin_y_{};
  int previous_tracking_dx_{};
  int last_x_{};
  bool can_flick_{};
  bool grabbed_between_slides_{};
  float origin_offset_{};
  std::optional<float> release_offset_;
  float velocity_{};
  int64_t drag_started_us_{};
  int64_t last_motion_us_{};
  float settle_from_{};
  float duration_us_{};
  float glide_duration_us_{default_glide_duration_us};
  int64_t settle_us_{};
};

}  // namespace espframe::native
