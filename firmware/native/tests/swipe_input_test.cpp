#include "screen_control.h"
#include "slide_dwell.h"
#include "swipe_input.h"

#include <cassert>
#include <limits>

using espframe::native::Swipe;
using espframe::native::SwipeInput;
using espframe::native::TouchReport;
using espframe::native::ScreenSchedule;
using espframe::native::ScreenAction;
using espframe::native::ScreenControl;
using espframe::native::screen_action;
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
  assert(input.contact_started_us() == 5'000);
  assert(input.take_first_motion() == 10'000);
  assert(input.take_first_motion() == 0);

  const auto first_handoff = swipe.position();
  input.update(swipe, report(3, 15'000, 0, 0), first_handoff);
  assert(swipe.settling() && !input.engaged());
  input.update(swipe, report(4, 20'000, 700), first_handoff);
  input.update(swipe, report(5, 25'000, 755), first_handoff);
  assert(swipe.dragging() && input.engaged());
  assert(input.contact_started_us() == 20'000);
  assert(swipe.position() == first_handoff - 55 && input.take_first_motion() == 25'000);
}

void an_upside_down_panel_keeps_the_same_viewer_swipe_direction() {
  using espframe::native::ScreenRotation;
  Swipe swipe{width, 5, 2};
  SwipeInput input;
  const auto send = [&](TouchReport touch) {
    espframe::native::orient_touch(touch, ScreenRotation::UpsideDown);
    assert(touch.y == 599);
    input.update(swipe, touch, swipe.position());
  };
  send(report(1, 5'000, 300));
  send(report(2, 15'000, 800));
  assert(swipe.dragging() && swipe.position() == initial_position + 500);
  send(report(3, 20'000, 0, 0));
  swipe.tick(300'000);
  assert(swipe.settled() == 3);

  send(report(4, 305'000, 850));
  send(report(5, 315'000, 350));
  assert(swipe.dragging() && swipe.position() == 3 * width - 500);
  send(report(6, 320'000, 0, 0));
  swipe.tick(600'000);
  assert(swipe.settled() == 2);
}

void a_held_photo_can_be_dragged_from_either_side() {
  Swipe swipe{width, 5, 2};
  SwipeInput input;
  const auto drag = [&](uint32_t sequence, int64_t start, int x, int y, int delta) {
    const auto send = [&](TouchReport touch) {
      touch.y = y;
      assert(input.update(swipe, touch, swipe.position()) == Effect::None);
    };
    send(report(sequence, start, x));
    send(report(sequence + 1, start + 3'100'000, x));
    const auto origin = swipe.position();
    send(report(sequence + 2, start + 3'300'000, x + delta));
    assert(swipe.dragging() && swipe.position() == origin - delta);
    send(report(sequence + 3, start + 3'350'000, 0, 0));
    swipe.tick(start + 3'600'000);
    assert(input.awake() && swipe.idle());
  };
  drag(1, 5'000, 1250, 24, -120);
  assert(swipe.settled() == 3);
  drag(5, 4'000'000, 24, 775, 120);
  assert(swipe.settled() == 2);
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

void scheduled_hours_preserve_manual_sleep_and_handle_midnight() {
  ScreenSchedule disabled{{false, 6, 22, 60}};
  assert(disabled.update(4, 100, false));
  ScreenSchedule all_day{{true, 8, 8, 60}};
  assert(all_day.update(0, 100, false) && all_day.update(8, 200, false));

  ScreenSchedule ordinary{{true, 6, 23, 60}};
  assert(ordinary.update(std::nullopt, 100, false));
  assert(!ordinary.update(5, 200, false));
  assert(ordinary.update(6, 300, false) && ordinary.update(22, 400, false));
  assert(ordinary.update(23, 450, true));
  assert(!ordinary.update(23, 500, false));
  ordinary.sleep();
  assert(!ordinary.update(6, 600, false));
  ordinary.wake(700);
  assert(ordinary.update(6, 800, false));

  ScreenSchedule overnight{{true, 22, 6, 60}};
  assert(!overnight.update(21, 100, false));
  assert(overnight.update(22, 200, false) && overnight.update(0, 300, false));
  assert(overnight.update(5, 400, false) && !overnight.update(6, 500, false));
}

void entering_the_on_window_clears_a_temporary_wake() {
  ScreenSchedule schedule{{true, 6, 23, 60}};
  assert(!schedule.update(5, 1'000, false));
  schedule.wake(5'000);
  assert(schedule.update(5, 10'000, false));
  assert(schedule.update(6, 20'000, false));
  // A local time correction can return to off-hours before the timer would
  // have expired. The previous off-hours tap no longer applies.
  assert(!schedule.update(5, 30'000, false));
}

void a_scheduled_wake_consumes_the_touch_and_expires_after_its_duration() {
  Swipe swipe{width, 5, 2};
  SwipeInput input;
  ScreenSchedule schedule{{true, 6, 22, 10}};
  assert(!schedule.update(23, 1'000, false));
  input.sleep(swipe);
  assert(input.update(swipe, report(1, 5'000, 900), initial_position) == Effect::Wake);
  schedule.wake(5'000);
  assert(schedule.update(23, 5'000, input.engaged()));
  assert(input.update(swipe, report(2, 10'000, 300), initial_position) == Effect::None);
  assert(swipe.idle() && swipe.settled() == 2);
  input.update(swipe, report(3, 15'000, 0, 0), initial_position);
  assert(schedule.update(23, 10'004'999, input.engaged()));

  input.update(swipe, report(4, 10'005'000, 800), initial_position);
  assert(schedule.update(23, 10'005'000, input.engaged()));
  input.update(swipe, report(5, 10'010'000, 0, 0), initial_position);
  assert(!schedule.update(23, 10'010'000, input.engaged()));
  input.sleep(swipe);
  assert(schedule.update(6, 10'020'000, false));
  input.wake();
  assert(input.update(swipe, report(6, 10'025'000, 800), initial_position) == Effect::None);
  input.update(swipe, report(7, 10'030'000, 740), initial_position);
  assert(swipe.dragging() && input.take_first_motion() == 10'030'000);
}

void web_commands_are_applied_through_the_same_schedule() {
  static_assert(screen_action("action=sleep") == ScreenAction::Sleep);
  static_assert(screen_action("action=wake") == ScreenAction::Wake);
  static_assert(screen_action("action=wake&") == ScreenAction::Idle);
  static_assert(screen_action("action=toggle") == ScreenAction::Idle);
  ScreenSchedule schedule{{true, 6, 22, 10}};
  ScreenControl control;
  assert(schedule.update(8, 0, false));
  assert(!control.apply_pending(schedule, 0));
  assert(!control.submit(ScreenAction::Idle));
  assert(control.submit(ScreenAction::Sleep));
  assert(!control.submit(ScreenAction::Wake));
  assert(control.apply_pending(schedule, 500));
  assert(!schedule.update(8, 500, false));
  assert(control.pending() && !control.submit(ScreenAction::Wake));
  control.completed();
  assert(!control.pending());
  assert(!schedule.update(9, 1'000, false));
  assert(control.submit(ScreenAction::Wake));
  assert(control.apply_pending(schedule, 1'500));
  assert(schedule.update(9, 1'500, false));
  control.completed();
  assert(schedule.update(9, 15'000'000, false));

  assert(!schedule.update(22, 16'000'000, false));
  assert(control.submit(ScreenAction::Wake));
  assert(control.apply_pending(schedule, 17'000'000));
  assert(schedule.update(22, 17'000'000, false));
  control.completed();
  assert(schedule.update(22, 26'999'999, false));
  assert(!schedule.update(22, 27'000'000, false));
}

void putting_the_screen_to_sleep_mid_swipe_waits_for_the_next_contact() {
  Swipe swipe{width, 5, 2};
  SwipeInput input;
  input.update(swipe, report(1, 5'000, 800), initial_position);
  input.update(swipe, report(2, 10'000, 700), initial_position);
  assert(swipe.dragging());
  input.sleep(swipe);
  assert(!input.awake() && swipe.idle() && swipe.position() == initial_position);
  assert(input.update(swipe, report(3, 15'000, 620), initial_position) == Effect::None);
  assert(input.update(swipe, report(4, 20'000, 0, 0), initial_position) == Effect::None);
  assert(!input.awake() && !input.engaged());
  assert(input.update(swipe, report(5, 25'000, 700), initial_position) == Effect::Wake);
  assert(swipe.idle() && swipe.settled() == 2);
}

}  // namespace

int main() {
  ordered_reports_keep_release_and_next_press();
  an_upside_down_panel_keeps_the_same_viewer_swipe_direction();
  a_held_photo_can_be_dragged_from_either_side();
  a_gap_cancels_without_flinging_into_another_photo();
  a_transient_transfer_failure_preserves_the_drag();
  a_second_finger_blocks_until_release_even_at_sequence_rollover();
  a_hold_sleeps_on_release_and_the_wake_contact_never_swipes();
  only_a_continuous_stationary_hold_can_sleep();
  scheduled_hours_preserve_manual_sleep_and_handle_midnight();
  entering_the_on_window_clears_a_temporary_wake();
  a_scheduled_wake_consumes_the_touch_and_expires_after_its_duration();
  web_commands_are_applied_through_the_same_schedule();
  putting_the_screen_to_sleep_mid_swipe_waits_for_the_next_contact();
}
