#pragma once

#include "slideshow_transition.h"

#include <atomic>

namespace espframe::native {

struct PlaybackTransition {
  uint16_t duration_ms{500};
  esphome::espframe::SlideTransition effect{esphome::espframe::SlideTransition::CROSS_FADE};
  bool random{};

  static PlaybackTransition from_settings(const std::string &effect, const std::string &speed) {
    using namespace esphome::espframe;
    return {static_cast<uint16_t>(slide_transition_duration_ms(speed)), parse_slide_transition(effect),
            slide_transition_is_random(effect)};
  }
};

// Read the effect and its duration together when an automatic move starts.
inline std::atomic<PlaybackTransition> active_playback_transition{};

}  // namespace espframe::native
