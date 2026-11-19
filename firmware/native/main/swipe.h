#pragma once

#include "slide_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace espframe::native {

class Swipe {
 public:
  enum class Transition { Quick, Glide };

  Swipe(int width, SlideIndex slide_count, SlideIndex initial_slide)
      : width_(width), ready_last_(slide_count - 1), settled_(initial_slide), target_(initial_slide) {}

  PixelPosition position() const {
    return this->settled_ * this->width_ + static_cast<PixelPosition>(std::floor(this->offset_ + 0.5f));
  }
  SlideIndex settled() const { return this->settled_; }
  bool idle() const { return this->phase_ == Phase::Idle; }
  bool dragging() const { return this->phase_ == Phase::Dragging; }
  bool settling() const { return this->phase_ == Phase::Settling; }

  void set_ready_range(SlideIndex first, SlideIndex last) {
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
    this->origin_x_ = x;
    this->origin_y_ = y;
    this->last_x_ = x;
    this->last_motion_us_ = now_us;
    this->velocity_ = 0;
    this->phase_ = Phase::Tracking;
  }

  void move(int x, int y, int64_t now_us) {
    if (this->phase_ == Phase::Tracking) {
      const int dx = std::abs(x - this->origin_x_);
      const int dy = std::abs(y - this->origin_y_);
      if (dy >= 16 && dy > dx) {
        this->phase_ = Phase::Ignored;
        return;
      }
      if (dx < 16 || dx * 2 < dy * 3)
        return;
      this->phase_ = Phase::Dragging;
    }
    if (this->phase_ != Phase::Dragging)
      return;

    if (this->last_x_ != x) {
      const int64_t elapsed = now_us - this->last_motion_us_;
      const float velocity = static_cast<float>(this->last_x_ - x) * 1000.0f /
                             std::max<int64_t>(elapsed, 1'000);
      this->velocity_ = elapsed < 85'000 ? 0.5f * (this->velocity_ + velocity) : 0.0f;
      this->last_motion_us_ = now_us;
      this->last_x_ = x;
    }
    this->offset_ = std::clamp(this->origin_offset_ + this->origin_x_ - x,
                               this->slide_offset_(this->minimum_slide_()),
                               this->slide_offset_(this->maximum_slide_()));
  }

  void release(int64_t now_us) {
    if (this->phase_ == Phase::Tracking || this->phase_ == Phase::Ignored) {
      this->settle_to_(this->resume_, now_us, this->resume_transition_);
      return;
    }
    if (this->phase_ != Phase::Dragging)
      return;

    const float velocity = now_us - this->last_motion_us_ < 85'000
                               ? std::clamp(this->velocity_, -3.0f, 3.0f)
                               : 0.0f;
    const float projected = this->offset_ + velocity * 170.0f;
    const float from_center = projected - this->slide_offset_(this->origin_slide_);
    const float threshold = this->width_ * 0.30f;
    int direction = 0;
    if (from_center >= threshold)
      direction = 1;
    else if (from_center <= -threshold)
      direction = -1;
    const auto destination =
        std::clamp(this->origin_slide_ + direction, this->minimum_slide_(), this->maximum_slide_());
    this->settle_to_(destination, now_us, Transition::Quick);
  }

  void cancel(int64_t now_us) {
    if (this->phase_ == Phase::Dragging || this->phase_ == Phase::Tracking ||
        this->phase_ == Phase::Ignored)
      this->settle_to_(this->resume_, now_us, this->resume_transition_);
  }

  bool advance(int64_t now_us, Transition transition) {
    if (this->phase_ != Phase::Idle || this->settled_ >= this->ready_last_)
      return false;
    this->settle_to_(this->settled_ + 1, now_us, transition);
    return true;
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

  // Animation arithmetic stays near zero even after millions of photos.
  float slide_offset_(SlideIndex slide) const { return static_cast<float>((slide - this->settled_) * this->width_); }

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
    this->duration_us_ = transition == Transition::Glide ? std::max(180'000.0f, 640'000.0f * fraction)
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
  int origin_x_{};
  int origin_y_{};
  int last_x_{};
  float origin_offset_{};
  float velocity_{};
  int64_t last_motion_us_{};
  float settle_from_{};
  float duration_us_{};
  int64_t settle_us_{};
};

}  // namespace espframe::native
