#include "playback_trace.h"

#include <cassert>

using namespace espframe::native;

int main() {
  constexpr auto left_edge = 3 * slide_width;
  constexpr auto right_edge = 5 * slide_width;
  constexpr SlideRange ready{3, 5};
  constexpr SlideRange available{0, 9};
  assert(ready_at_motion(left_edge, ready, available).result == CacheRead::Hit);
  assert(ready_at_motion(right_edge - 1, ready, available).result == CacheRead::Hit);

  const auto left_miss = ready_at_motion(left_edge - 1, ready, available);
  assert(left_miss.result == CacheRead::Miss && left_miss.missing_slide == 2);
  const auto right_miss = ready_at_motion(right_edge + 1, ready, available);
  assert(right_miss.result == CacheRead::Miss && right_miss.missing_slide == 6);
  assert(ready_at_motion(left_edge - 1, ready, {3, 9}).result == CacheRead::Edge);
  assert(ready_at_motion(right_edge + 1, ready, {0, 5}).result == CacheRead::Edge);

  MotionEpoch motion;
  assert(motion.phase() == MotionPhase::Still);
  motion.update(MotionPhase::Drag);
  const auto first_frame = motion.stamp(true);
  motion.update(MotionPhase::Drag);
  const auto second_frame = motion.stamp(false);
  assert(first_frame.epoch == second_frame.epoch);
  assert(first_frame.preparing && !second_frame.preparing);

  motion.update(MotionPhase::Settle);
  assert(motion.stamp(false).epoch != first_frame.epoch);
  motion.update(MotionPhase::Still);
  motion.update(MotionPhase::Drag);
  assert(motion.stamp(false).epoch != first_frame.epoch);
  assert(first_frame.phase == MotionPhase::Drag);
}
