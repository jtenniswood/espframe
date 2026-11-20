#pragma once

#include "slide_geometry.h"

#include <algorithm>
#include <cstdint>

namespace espframe::native {

class AutoplayFade {
 public:
  static constexpr int64_t duration_us = 500'000;
  static constexpr uint8_t opaque = 255;

  void start(SlideIndex anchor, SlideIndex target, int64_t now, int64_t duration = duration_us) {
    anchor_ = anchor;
    target_ = target;
    started_us_ = now;
    duration_us_ = duration;
    submitted_ = 0;
    phase_ = Phase::Running;
  }

  bool running() const { return phase_ == Phase::Running; }
  bool held() const { return phase_ == Phase::Held; }
  SlideIndex anchor() const { return anchor_; }
  SlideIndex target() const { return target_; }

  uint8_t opacity(int64_t now) const {
    const float t = static_cast<float>(std::clamp(now - started_us_, int64_t{0}, duration_us_)) / duration_us_;
    return static_cast<uint8_t>(opaque * t * t * (3.0f - 2.0f * t));
  }
  uint8_t submitted_opacity() const { return submitted_; }
  void submitted(uint8_t opacity) { submitted_ = opacity; }

  // Only a photo handed to the panel can be picked up by a touch. A blend
  // still being composed must be discarded even if it has just finished.
  bool interrupt(bool has_snapshot) {
    const bool pin = submitted_ != 0;
    phase_ = pin || has_snapshot ? Phase::Held : Phase::Idle;
    dragged_ = false;
    return pin;
  }

  void note_drag() { dragged_ = true; }

  void resume(int64_t now) {
    const auto duration = dragged_ ? 130'000 : duration_us_ * (opaque - submitted_) / opaque;
    start(anchor_, dragged_ ? anchor_ : target_, now, std::max<int64_t>(duration, 130'000));
  }

  void finish() { phase_ = Phase::Idle; }

 private:
  enum class Phase { Idle, Running, Held };

  Phase phase_{Phase::Idle};
  SlideIndex anchor_{};
  SlideIndex target_{};
  int64_t started_us_{};
  int64_t duration_us_{duration_us};
  uint8_t submitted_{};
  bool dragged_{};
};

}  // namespace espframe::native
