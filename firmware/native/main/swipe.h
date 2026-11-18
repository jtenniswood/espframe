#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace espframe::native {

class Swipe {
 public:
  Swipe(int width, int slide_count, int initial_slide)
      : width_(width), last_(slide_count - 1), ready_last_(last_), position_(static_cast<float>(initial_slide * width)),
        settled_(initial_slide), target_(initial_slide) {}

  float position() const { return this->position_; }
  int settled() const { return this->settled_; }
  bool dragging() const { return this->phase_ == Phase::Dragging; }
  bool settling() const { return this->phase_ == Phase::Settling; }

  void set_ready_range(int first, int last) {
    this->ready_first_ = first;
    this->ready_last_ = last;
  }

  void press(int x, int y, int presented_position, int64_t now_us) {
    this->resume_ = this->target_;
    this->position_ = static_cast<float>(presented_position);
    this->origin_position_ = this->position_;
    this->origin_slide_ = this->nearest_slide_(this->position_);
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
    this->position_ = std::clamp(this->origin_position_ + this->origin_x_ - x,
                                 static_cast<float>(this->minimum_slide_() * this->width_),
                                 static_cast<float>(this->maximum_slide_() * this->width_));
  }

  void release(int64_t now_us) {
    if (this->phase_ == Phase::Tracking || this->phase_ == Phase::Ignored) {
      this->settle_to_(this->resume_, now_us);
      return;
    }
    if (this->phase_ != Phase::Dragging)
      return;

    const float velocity = now_us - this->last_motion_us_ < 85'000
                               ? std::clamp(this->velocity_, -3.0f, 3.0f)
                               : 0.0f;
    const float projected = this->position_ + velocity * 170.0f;
    const float from_center = projected - this->origin_slide_ * this->width_;
    const float threshold = this->width_ * 0.30f;
    int direction = 0;
    if (from_center >= threshold)
      direction = 1;
    else if (from_center <= -threshold)
      direction = -1;
    this->settle_to_(std::clamp(this->origin_slide_ + direction, this->minimum_slide_(),
                                this->maximum_slide_()), now_us);
  }

  void cancel(int64_t now_us) {
    if (this->phase_ == Phase::Dragging || this->phase_ == Phase::Tracking ||
        this->phase_ == Phase::Ignored)
      this->settle_to_(this->resume_, now_us);
  }

  void tick(int64_t now_us) {
    if (this->phase_ != Phase::Settling)
      return;

    const float t = std::clamp(static_cast<float>(now_us - this->settle_us_) / this->duration_us_,
                               0.0f, 1.0f);
    const float inverse = 1.0f - t;
    const float end = static_cast<float>(this->target_ * this->width_);
    this->position_ = end + (this->settle_from_ - end) * inverse * inverse * inverse;
    if (t == 1.0f) {
      this->settled_ = this->target_;
      this->phase_ = Phase::Idle;
    }
  }

 private:
  enum class Phase { Idle, Tracking, Ignored, Dragging, Settling };

  int nearest_slide_(float pixels) const {
    return std::clamp(static_cast<int>(std::lround(pixels / this->width_)), 0, this->last_);
  }

  int minimum_slide_() const { return std::max(this->ready_first_, this->origin_slide_ - 1); }
  int maximum_slide_() const { return std::min(this->ready_last_, this->origin_slide_ + 1); }

  void settle_to_(int slide, int64_t now_us) {
    this->target_ = slide;
    this->settle_from_ = this->position_;
    this->settle_us_ = now_us;
    const float distance = std::abs(this->position_ - slide * this->width_);
    if (distance < 0.5f) {
      this->position_ = static_cast<float>(slide * this->width_);
      this->settled_ = slide;
      this->phase_ = Phase::Idle;
      return;
    }
    this->duration_us_ = 120'000.0f + 100'000.0f * std::min(distance / this->width_, 1.0f);
    this->phase_ = Phase::Settling;
  }

  int width_;
  int last_;
  int ready_first_{};
  int ready_last_;
  float position_;
  int settled_;
  int target_;
  Phase phase_{Phase::Idle};
  int resume_{};
  int origin_slide_{};
  int origin_x_{};
  int origin_y_{};
  int last_x_{};
  float origin_position_{};
  float velocity_{};
  int64_t last_motion_us_{};
  float settle_from_{};
  float duration_us_{};
  int64_t settle_us_{};
};

}  // namespace espframe::native
