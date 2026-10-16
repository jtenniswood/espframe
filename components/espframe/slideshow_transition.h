#pragma once

#include <cstdint>
#include <string>

namespace esphome::espframe {

enum class SlideTransition : uint8_t {
  NONE,
  CROSS_FADE,
  FADE_BLACK,
  FADE_WHITE,
  SLIDE_LEFT,
  SLIDE_RIGHT,
  SLIDE_UP,
  SLIDE_DOWN,
};

// Random draws from every animated transition.
static constexpr SlideTransition RANDOM_SLIDE_TRANSITIONS[] = {
    SlideTransition::CROSS_FADE, SlideTransition::FADE_BLACK,  SlideTransition::FADE_WHITE, SlideTransition::SLIDE_LEFT,
    SlideTransition::SLIDE_RIGHT, SlideTransition::SLIDE_UP, SlideTransition::SLIDE_DOWN,
};
static constexpr int RANDOM_SLIDE_TRANSITION_COUNT =
    sizeof(RANDOM_SLIDE_TRANSITIONS) / sizeof(RANDOM_SLIDE_TRANSITIONS[0]);

inline bool slide_transition_is_random(const std::string &option) { return option == "Random"; }

inline SlideTransition parse_slide_transition(const std::string &option) {
  if (option == "Cross Fade") return SlideTransition::CROSS_FADE;
  if (option == "Fade to Black") return SlideTransition::FADE_BLACK;
  if (option == "Fade to White") return SlideTransition::FADE_WHITE;
  if (option == "Slide Left") return SlideTransition::SLIDE_LEFT;
  if (option == "Slide Right") return SlideTransition::SLIDE_RIGHT;
  if (option == "Slide Up") return SlideTransition::SLIDE_UP;
  if (option == "Slide Down") return SlideTransition::SLIDE_DOWN;
  return SlideTransition::NONE;
}

// Never repeats the previous pick, so consecutive photos always look different.
inline SlideTransition pick_random_slide_transition(uint32_t random, SlideTransition previous) {
  int previous_index = -1;
  for (int i = 0; i < RANDOM_SLIDE_TRANSITION_COUNT; ++i) {
    if (RANDOM_SLIDE_TRANSITIONS[i] == previous) previous_index = i;
  }
  const int choices = previous_index < 0 ? RANDOM_SLIDE_TRANSITION_COUNT : RANDOM_SLIDE_TRANSITION_COUNT - 1;
  int index = static_cast<int>(random % static_cast<uint32_t>(choices));
  if (previous_index >= 0 && index >= previous_index) index++;
  return RANDOM_SLIDE_TRANSITIONS[index];
}

inline SlideTransition resolve_slide_transition(const std::string &option, uint32_t random,
                                                SlideTransition previous) {
  if (slide_transition_is_random(option)) return pick_random_slide_transition(random, previous);
  return parse_slide_transition(option);
}

// Going back through history mirrors directional transitions.
inline SlideTransition reverse_slide_transition(SlideTransition transition) {
  switch (transition) {
    case SlideTransition::SLIDE_LEFT: return SlideTransition::SLIDE_RIGHT;
    case SlideTransition::SLIDE_RIGHT: return SlideTransition::SLIDE_LEFT;
    case SlideTransition::SLIDE_UP: return SlideTransition::SLIDE_DOWN;
    case SlideTransition::SLIDE_DOWN: return SlideTransition::SLIDE_UP;
    default: return transition;
  }
}

inline uint32_t slide_transition_duration_ms(const std::string &speed) {
  if (speed == "Fast") return 500;
  if (speed == "Slow") return 1500;
  return 1000;
}

// What the display driver composites for a transition, independent of the
// driver's own types so it stays host-testable.
enum class TransitionBlend : uint8_t { NONE, CROSS_FADE, FADE_THROUGH, MOVE };

struct TransitionPlan {
  TransitionBlend blend = TransitionBlend::NONE;
  uint32_t color_rgb888 = 0;
  // MOVE: direction the new photo travels, in screen (not panel) pixels.
  int8_t dx = 0;
  int8_t dy = 0;
};

inline TransitionPlan slide_transition_plan(SlideTransition transition) {
  TransitionPlan plan;
  switch (transition) {
    case SlideTransition::CROSS_FADE: plan.blend = TransitionBlend::CROSS_FADE; break;
    case SlideTransition::FADE_BLACK: plan.blend = TransitionBlend::FADE_THROUGH; break;
    case SlideTransition::FADE_WHITE:
      plan.blend = TransitionBlend::FADE_THROUGH;
      plan.color_rgb888 = 0xFFFFFF;
      break;
    case SlideTransition::SLIDE_LEFT: plan = {TransitionBlend::MOVE, 0, -1, 0}; break;
    case SlideTransition::SLIDE_RIGHT: plan = {TransitionBlend::MOVE, 0, 1, 0}; break;
    case SlideTransition::SLIDE_UP: plan = {TransitionBlend::MOVE, 0, 0, -1}; break;
    case SlideTransition::SLIDE_DOWN: plan = {TransitionBlend::MOVE, 0, 0, 1}; break;
    default: break;
  }
  return plan;
}

// Maps a screen direction onto the panel's native axes for the clockwise
// screen rotation LVGL renders with (matches ESPHome's flush mapping).
inline void screen_to_panel_direction(int rotation_degrees, int8_t &dx, int8_t &dy) {
  const int8_t x = dx, y = dy;
  switch (((rotation_degrees % 360) + 360) % 360) {
    case 90: dx = -y; dy = x; break;
    case 180: dx = -x; dy = -y; break;
    case 270: dx = y; dy = -x; break;
    default: break;
  }
}

}  // namespace esphome::espframe
