#include "slide_dwell.h"
#include "swipe_input.h"

#include <cassert>
#include <limits>

using espframe::native::Swipe;
using espframe::native::SwipeInput;
using espframe::native::TouchReport;
using Effect = SwipeInput::Effect;

namespace {

constexpr int width = 1280;
constexpr int64_t initial_position = width * 2;

TouchReport report(uint32_t sequence, int64_t time, int x = 0, uint8_t contacts = 1, bool valid = true) {
  return {x, 200, contacts, valid, sequence, time};
}

void ordered_reports_keep_release_and_next_press() {
  Swipe swipe{width, 5, 2};
  SwipeInput input;
  input.update(swipe, report(1, 5'000, 800), initial_position);
  input.update(swipe, report(2, 10'000, 700), initial_position);
  assert(swipe.dragging() && input.engaged());
  assert(swipe.position() == initial_position + 100);
  assert(input.take_first_motion() == 10'000);
  assert(input.take_first_motion() == 0);

  const auto first_handoff = swipe.position();
  input.update(swipe, report(3, 15'000, 0, 0), first_handoff);
  assert(swipe.settling() && !input.engaged());
  input.update(swipe, report(4, 20'000, 700), first_handoff);
  input.update(swipe, report(5, 25'000, 755), first_handoff);
  assert(swipe.dragging() && input.engaged());
  assert(swipe.position() == first_handoff - 55 && input.take_first_motion() == 25'000);
}

void a_gap_cancels_without_flinging_into_another_photo() {
  Swipe swipe{width, 5, 2};
  SwipeInput input;
  input.update(swipe, report(1, 5'000, 800), initial_position);
  input.update(swipe, report(2, 10'000, 300), initial_position);
  assert(swipe.dragging());

  const auto handoff = swipe.position();
  input.update(swipe, report(11, 55'000, 250), handoff);
  assert(swipe.settling() && input.engaged());
  assert(input.take_first_motion() == 0);
  input.update(swipe, report(12, 60'000, 200), handoff);
  assert(!swipe.dragging() && input.engaged());
  input.update(swipe, report(13, 65'000, 0, 0), handoff);
  swipe.tick(400'000);
  assert(swipe.idle() && swipe.settled() == 2 && !input.engaged());

  input.update(swipe, report(14, 405'000, 500), initial_position);
  input.update(swipe, report(15, 410'000, 530), initial_position);
  assert(swipe.dragging() && swipe.position() == initial_position - 30);
}

void a_transient_transfer_failure_preserves_the_drag() {
  Swipe swipe{width, 5, 2};
  SwipeInput input;
  input.update(swipe, report(1, 5'000, 800), initial_position);
  input.update(swipe, report(2, 10'000, 750), initial_position);
  input.update(swipe, report(3, 22'000, 0, 0, false), swipe.position());
  input.update(swipe, report(4, 34'000, 0, 0, false), swipe.position());
  assert(swipe.dragging() && input.engaged());
  input.update(swipe, report(5, 39'000, 740), swipe.position());
  assert(swipe.position() == initial_position + 60 && swipe.dragging());

  for (uint32_t sequence = 6; sequence <= 8; ++sequence)
    input.update(swipe, report(sequence, sequence * 12'000, 0, 0, false), swipe.position());
  assert(swipe.settling() && input.engaged());
  input.update(swipe, report(9, 108'000, 0, 0), swipe.position());
  assert(!input.engaged());
}

void a_second_finger_blocks_until_release_even_at_sequence_rollover() {
  Swipe swipe{width, 5, 2};
  SwipeInput input;
  constexpr auto last = std::numeric_limits<uint32_t>::max();
  input.update(swipe, report(last, 5'000, 800), initial_position);
  input.update(swipe, report(0, 10'000, 730), initial_position);
  assert(swipe.dragging());
  input.update(swipe, report(1, 15'000, 600, 2), swipe.position());
  input.update(swipe, report(2, 20'000, 500), swipe.position());
  assert(swipe.settling() && input.engaged());
  input.update(swipe, report(3, 25'000, 0, 0), swipe.position());
  assert(input.take_first_motion() == 0 && !input.engaged());
}

void a_hold_sleeps_on_release_and_the_wake_contact_never_swipes() {
  Swipe swipe{width, 5, 2};
  SwipeInput input;
  espframe::native::SlideDwell dwell;
  constexpr int64_t interval = 15'000'000;
  assert(!dwell.elapsed(0, true, interval));
  assert(input.update(swipe, report(1, 5'000, 800), initial_position) == Effect::None);
  assert(input.update(swipe, report(2, 3'005'000, 808), initial_position) == Effect::None);
  assert(input.awake());
  assert(input.update(swipe, report(3, 3'010'000, 0, 0), initial_position) == Effect::Sleep);
  assert(!input.awake() && swipe.idle() && swipe.settled() == 2);
  assert(!dwell.elapsed(60'000'000, input.awake(), interval));

  assert(input.update(swipe, report(4, 60'005'000, 820, 2), initial_position) == Effect::None);
  assert(input.update(swipe, report(5, 60'010'000, 820), initial_position) == Effect::None);
  assert(!input.awake());
  input.update(swipe, report(6, 60'015'000, 0, 0), initial_position);
  assert(input.update(swipe, report(7, 60'020'000, 820), initial_position) == Effect::Wake);
  assert(input.awake() && input.engaged());
  assert(!dwell.elapsed(60'020'000, input.awake() && !input.engaged(), interval));
  assert(input.update(swipe, report(8, 64'000'000, 240), initial_position) == Effect::None);
  assert(input.take_first_motion() == 0 && swipe.idle() && swipe.position() == initial_position);
  assert(input.update(swipe, report(9, 64'010'000, 0, 0), initial_position) == Effect::None);
  assert(input.awake() && !input.engaged() && swipe.settled() == 2);
  assert(!dwell.elapsed(64'010'000, true, interval));
  assert(!dwell.elapsed(79'009'999, true, interval));
  assert(dwell.elapsed(79'010'000, true, interval));

  input.update(swipe, report(10, 80'005'000, 800), initial_position);
  input.update(swipe, report(11, 80'015'000, 280), initial_position);
  assert(swipe.dragging() && input.take_first_motion() == 80'015'000);
  input.update(swipe, report(12, 80'020'000, 0, 0), initial_position);
  swipe.tick(81'000'000);
  assert(swipe.settled() == 3 && input.awake());
}

void only_a_continuous_stationary_hold_can_sleep() {
  Swipe swipe{width, 5, 2};
  SwipeInput input;
  input.update(swipe, report(1, 5'000, 800), initial_position);
  assert(input.update(swipe, report(2, 3'004'999, 0, 0), initial_position) == Effect::None);
  input.update(swipe, report(3, 4'000'000, 800), initial_position);
  auto moved = report(4, 4'005'000, 800);
  moved.y += 13;
  input.update(swipe, moved, initial_position);
  assert(input.update(swipe, report(5, 7'010'000, 0, 0), initial_position) == Effect::None);
  input.update(swipe, report(6, 8'000'000, 800), initial_position);
  input.update(swipe, report(7, 8'005'000, 0, 0, false), initial_position);
  assert(input.update(swipe, report(8, 11'010'000, 0, 0), initial_position) == Effect::None);
  input.update(swipe, report(9, 12'000'000, 800), initial_position);
  assert(input.update(swipe, report(11, 15'010'000, 0, 0), initial_position) == Effect::None);
  assert(input.awake() && swipe.settled() == 2);
}

}  // namespace

int main() {
  ordered_reports_keep_release_and_next_press();
  a_gap_cancels_without_flinging_into_another_photo();
  a_transient_transfer_failure_preserves_the_drag();
  a_second_finger_blocks_until_release_even_at_sequence_rollover();
  a_hold_sleeps_on_release_and_the_wake_contact_never_swipes();
  only_a_continuous_stationary_hold_can_sleep();
}
