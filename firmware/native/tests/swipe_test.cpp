#include "slide_blit.h"
#include "slide_dwell.h"
#include "slideshow_progress_ring.h"
#include "swipe.h"

#include <array>
#include <cassert>
#include <vector>

using espframe::native::Swipe;
using espframe::native::SlideIndex;
using espframe::native::PixelPosition;
using espframe::native::SlideDwell;

int main() {
  constexpr int width = 1280;
  constexpr int64_t start = 1'000'000;

  using Activity = Swipe::DragActivity;
  constexpr int64_t motion_window = 60'000;
  constexpr int64_t start_guard = 120'000;
  Swipe held{width, 5, 2};
  const auto activity = [&](int64_t elapsed) { return held.drag_activity(start + elapsed, motion_window, start_guard); };
  held.press(900, 400, 2 * width, start);
  assert(activity(1'000'000) == Activity::Still);
  held.move(870, 400, start + 1'000'000);
  assert(activity(1'000'000) == Activity::Starting);
  held.move(800, 400, start + 1'058'000);
  assert(activity(1'117'000) == Activity::Starting);
  held.move(800, 400, start + 1'118'000);
  assert(activity(1'118'000) == Activity::Still);
  held.move(790, 400, start + 1'119'000);
  assert(activity(1'119'000) == Activity::Starting);
  assert(activity(1'120'000) == Activity::Moving);
  held.move(780, 400, start + 1'200'000);
  assert(activity(1'200'000) == Activity::Moving);
  held.release(start + 1'210'000);
  assert(activity(1'210'000) == Activity::Still);

  Swipe slow{width, 5, 2};
  slow.press(900, 400, 2 * width, start - 10'000);
  slow.move(895, 400, start);
  assert(slow.position() == 2 * width && !slow.dragging());
  slow.move(810, 401, start + 150'000);
  assert(slow.dragging() && slow.position() == 2 * width + 90);
  slow.release(start + 250'000);
  assert(slow.settling() && slow.settled() == 2);
  slow.tick(start + 500'000);
  assert(slow.position() == 2 * width && slow.settled() == 2);

  Swipe short_swipe{width, 5, 2};
  short_swipe.press(900, 400, 2 * width, start);
  short_swipe.move(780, 402, start + 180'000);
  assert(short_swipe.position() == 2 * width + 120);
  short_swipe.release(start + 200'000);
  short_swipe.tick(start + 450'000);
  assert(short_swipe.settled() == 3);

  short_swipe.press(500, 400, 3 * width, start + 500'000);
  short_swipe.move(620, 398, start + 680'000);
  assert(short_swipe.position() == 3 * width - 120);
  short_swipe.release(start + 700'000);
  short_swipe.tick(start + 950'000);
  assert(short_swipe.settled() == 2);

  Swipe flick{width, 5, 2};
  flick.press(900, 400, 2 * width, start - 25'000);
  flick.move(850, 400, start);
  flick.move(700, 398, start + 30'000);
  flick.release(start + 40'000);
  assert(flick.settling() && flick.settled() == 2);
  flick.tick(start + 65'000);
  const int visible = static_cast<int>(flick.position());
  assert(visible > 2 * width && visible < 3 * width);
  flick.press(650, 390, visible, start + 65'000);
  assert(flick.position() == visible);
  flick.move(699, 390, start + 75'000);
  flick.move(890, 390, start + 105'000);
  flick.release(start + 130'000);
  flick.tick(start + 500'000);
  assert(flick.position() == 2 * width && flick.settled() == 2);

  Swipe one_sample{width, 5, 2};
  one_sample.press(760, 250, 2 * width, start);
  one_sample.move(620, 250, start + 20'000);
  one_sample.move(620, 250, start + 23'000);
  one_sample.release(start + 29'000);
  one_sample.tick(start + 300'000);
  assert(one_sample.settled() == 3);

  Swipe repeated{width, 5, 1};
  repeated.press(900, 390, width, start - 100'000);
  repeated.move(320, 390, start);
  repeated.release(start + 10'000);
  repeated.tick(start + 85'000);
  const int in_flight = static_cast<int>(repeated.position());
  assert(in_flight > width + width / 2 && in_flight < 2 * width);
  repeated.press(910, 390, in_flight, start + 85'000);
  repeated.move(300, 390, start + 110'000);
  repeated.release(start + 125'000);
  repeated.tick(start + 500'000);
  assert(repeated.settled() == 3);

  Swipe cancelled{width, 5, 2};
  cancelled.press(900, 390, 2 * width, start - 100'000);
  cancelled.move(300, 390, start);
  cancelled.cancel(start + 10'000);
  cancelled.tick(start + 400'000);
  assert(cancelled.settled() == 2 && cancelled.position() == 2 * width);

  Swipe edge{width, 5, 0};
  edge.press(300, 390, 0, start - 100'000);
  edge.move(900, 390, start);
  assert(edge.position() < 0 && edge.position() > -48);
  edge.release(start + 50'000);
  assert(edge.settling());
  edge.tick(start + 300'000);
  assert(edge.position() == 0 && !edge.settling());
  edge.press(300, 200, 0, start + 300'000);
  edge.move(280, 250, start + 350'000);
  edge.move(40, 250, start + 450'000);
  edge.release(start + 500'000);
  assert(edge.position() == 0 && !edge.settling());

  Swipe cold_boundary{width, 5, 2};
  cold_boundary.set_ready_range(1, 2);
  cold_boundary.press(950, 300, 2 * width, start);
  cold_boundary.move(300, 300, start + 80'000);
  const auto resisted = cold_boundary.position();
  assert(resisted > 2 * width && resisted < 2 * width + 48);
  cold_boundary.set_ready_range(1, 3);
  cold_boundary.move(300, 300, start + 100'000);
  assert(cold_boundary.position() == resisted);
  cold_boundary.move(265, 300, start + 110'000);
  assert(cold_boundary.position() == resisted + 35);
  cold_boundary.release(start + 120'000);
  cold_boundary.tick(start + 600'000);
  assert(cold_boundary.settled() == 3);

  Swipe older_refill{width, 5, 2};
  older_refill.set_ready_range(2, 3);
  older_refill.press(150, 300, 2 * width, start);
  older_refill.move(800, 300, start + 80'000);
  const auto resisted_previous = older_refill.position();
  assert(resisted_previous < 2 * width && resisted_previous > 2 * width - 32);
  older_refill.set_ready_range(1, 3);
  older_refill.move(800, 300, start + 100'000);
  assert(older_refill.position() == resisted_previous);
  older_refill.move(835, 300, start + 110'000);
  assert(older_refill.position() == resisted_previous - 35);
  older_refill.release(start + 120'000);
  older_refill.tick(start + 600'000);
  assert(older_refill.settled() == 1);

  // A refill must not undo a committed drag even if the finger holds still
  // until its velocity expires. Exercise either edge and a short drag.
  for (const int direction : {-1, 1}) {
    for (const int distance : {65, 120, 300}) {
      Swipe refilled{width, 5, 2};
      refilled.set_ready_range(2, 2);
      refilled.press(650, 300, 2 * width, start);
      refilled.move(650 - distance * direction, 300, start + 110'000);
      const auto held = refilled.position();
      refilled.set_ready_range(1, 3);
      refilled.move(650 - distance * direction, 300, start + 250'000);
      assert(refilled.position() == held);
      refilled.release(start + 260'000);
      refilled.tick(start + 500'000);
      assert(refilled.idle() && refilled.settled() == (distance < 120 ? 2 : 2 + direction));
    }
  }

  Swipe reversed_refill{width, 5, 2};
  reversed_refill.set_ready_range(1, 2);
  reversed_refill.press(900, 300, 2 * width, start);
  reversed_refill.move(400, 300, start + 110'000);
  const auto held_before_reverse = reversed_refill.position();
  reversed_refill.set_ready_range(1, 3);
  reversed_refill.move(400 + static_cast<int>(held_before_reverse - 2 * width) + 1, 300, start + 220'000);
  assert(reversed_refill.position() < 2 * width);
  reversed_refill.move(400, 300, start + 330'000);
  assert(reversed_refill.position() == held_before_reverse);
  reversed_refill.release(start + 440'000);
  reversed_refill.tick(start + 690'000);
  assert(reversed_refill.idle() && reversed_refill.settled() == 2);

  Swipe reversed_to_neighbor{width, 5, 2};
  reversed_to_neighbor.set_ready_range(1, 2);
  reversed_to_neighbor.press(900, 300, 2 * width, start);
  reversed_to_neighbor.move(400, 300, start + 110'000);
  reversed_to_neighbor.set_ready_range(1, 3);
  reversed_to_neighbor.move(650, 300, start + 220'000);
  reversed_to_neighbor.release(start + 330'000);
  reversed_to_neighbor.tick(start + 580'000);
  assert(reversed_to_neighbor.idle() && reversed_to_neighbor.settled() == 1);

  Swipe cancelled_refill{width, 5, 2};
  cancelled_refill.set_ready_range(1, 2);
  cancelled_refill.press(900, 300, 2 * width, start);
  cancelled_refill.move(400, 300, start + 110'000);
  cancelled_refill.set_ready_range(1, 3);
  cancelled_refill.cancel(start + 140'000);
  cancelled_refill.tick(start + 390'000);
  assert(cancelled_refill.idle() && cancelled_refill.settled() == 2);

  // Either miss uses the same pull and snap-back. A refill during that
  // animation becomes swipeable on the next gesture.
  constexpr int64_t released = start + 55'000;
  PixelPosition forward_pull{};
  PixelPosition forward_return{};
  for (const int direction : {1, -1}) {
    Swipe boundary{width, 5, 2};
    boundary.set_ready_range(direction > 0 ? 1 : 2, direction > 0 ? 2 : 3);
    const int from = direction > 0 ? 900 : 100;
    const int to = from - 800 * direction;
    boundary.press(from, 400, 2 * width, start);
    boundary.move(to, 400, start + 50'000);
    const auto pull = (boundary.position() - 2 * width) * direction;
    assert(pull > 0 && pull <= 32);
    boundary.release(released);
    assert(boundary.settling() && boundary.settle_destination() == 2);
    boundary.tick(released + 45'000);
    const auto returning = (boundary.position() - 2 * width) * direction;
    assert(returning > 0 && returning < pull);
    if (direction > 0) {
      forward_pull = pull;
      forward_return = returning;
    } else {
      assert(pull == forward_pull && returning == forward_return);
    }
    boundary.set_ready_range(1, 3);
    boundary.tick(released + 180'000);
    assert(boundary.idle() && boundary.settled() == 2);
    boundary.press(from, 400, 2 * width, released + 200'000);
    boundary.move(to, 400, released + 250'000);
    boundary.release(released + 260'000);
    boundary.tick(released + 510'000);
    assert(boundary.idle() && boundary.settled() == 2 + direction);
  }

  // A fresh touch owns the screen, even if the old neighbour loads during it.
  Swipe retake{width, 5, 2};
  retake.set_ready_range(1, 2);
  retake.press(900, 400, 2 * width, start);
  retake.move(100, 400, start + 50'000);
  retake.release(released);
  retake.press(500, 400, retake.position(), released + 200'000);
  retake.set_ready_range(1, 3);
  retake.release(released + 240'000);
  retake.tick(released + 600'000);
  assert(retake.idle() && retake.settled() == 2);

  // Grabbing a resisted edge starts from the visible pull. A fresh drag
  // can pull farther, retreat toward the photo, and request the missing slide.
  for (const int direction : {-1, 1}) {
    Swipe edge_grab{width, 5, 2};
    edge_grab.set_ready_range(direction > 0 ? 1 : 2, direction > 0 ? 2 : 3);
    edge_grab.press(650, 400, 2 * width, start);
    edge_grab.move(650 - 600 * direction, 400, start + 110'000);
    edge_grab.release(start + 120'000);
    const auto visible = edge_grab.position();
    assert((visible - 2 * width) * direction > 20);
    assert(edge_grab.settling() && edge_grab.settle_destination() == 2);

    edge_grab.press(650, 400, visible, start + 200'000);
    assert(edge_grab.position() == visible && !edge_grab.settling());
    edge_grab.move(650 - 32 * direction, 400, start + 310'000);
    assert((edge_grab.position() - visible) * direction > 0);
    assert((edge_grab.position() - 2 * width) * direction <= 32);
    edge_grab.move(650 + 20 * direction, 400, start + 420'000);
    assert(edge_grab.position() == visible - 20 * direction);
    edge_grab.move(650 - 160 * direction, 400, start + 530'000);
    edge_grab.release(start + 640'000);
    assert(edge_grab.settling() && edge_grab.settle_destination() == 2);
    edge_grab.set_ready_range(1, 3);
    edge_grab.tick(start + 700'000);
    edge_grab.tick(start + 930'000);
    assert(edge_grab.idle() && edge_grab.settled() == 2);
  }

  // An evicted history position is a true end, even when the panel has pixels for a newer neighbour.
  Swipe oldest{width, 5, 2};
  oldest.set_ready_range(2, 3);
  oldest.press(200, 400, 2 * width, start);
  oldest.move(900, 400, start + 50'000);
  oldest.release(released);
  oldest.tick(released + 200'000);
  assert(oldest.idle() && oldest.settled() == 2);

  Swipe short_edge{width, 5, 2};
  short_edge.set_ready_range(1, 2);
  short_edge.press(900, 400, 2 * width, start);
  short_edge.move(800, 400, start + 110'000);
  short_edge.release(start + 220'000);
  short_edge.tick(start + 420'000);
  assert(short_edge.idle() && short_edge.settled() == 2);

  Swipe autoplay{width, 5, 0};
  autoplay.set_ready_range(0, 0);
  assert(!autoplay.advance(start, Swipe::Transition::Quick));
  autoplay.set_ready_range(0, 1);
  autoplay.press(950, 300, 0, start);
  assert(!autoplay.advance(start, Swipe::Transition::Quick));
  autoplay.release(start + 20'000);
  assert(autoplay.advance(start + 30'000, Swipe::Transition::Quick));
  assert(!autoplay.advance(start + 40'000, Swipe::Transition::Quick));
  autoplay.tick(start + 500'000);
  assert(autoplay.settled() == 1 && !autoplay.advance(start + 1'000'000, Swipe::Transition::Glide));

  constexpr SlideIndex far = 3'000'000'000;
  constexpr PixelPosition far_pixels = far * width;
  Swipe long_running{width, far + 5, far};
  long_running.set_ready_range(far - 1, far + 1);
  assert(long_running.position() == far_pixels);
  long_running.press(600, 300, far_pixels, start);
  long_running.move(569, 300, start + 30'000);
  assert(long_running.position() == far_pixels + 31);
  long_running.move(568, 300, start + 60'000);
  assert(long_running.position() == far_pixels + 32);
  long_running.move(635, 300, start + 70'000);
  assert(long_running.position() == far_pixels - 35);
  long_running.release(start + 300'000);
  long_running.tick(start + 600'000);
  assert(long_running.position() == far_pixels);
  assert(long_running.advance(start + 700'000, Swipe::Transition::Quick));
  long_running.tick(start + 730'000);
  const auto moving = long_running.position();
  assert(moving > far_pixels && moving < far_pixels + width);
  long_running.press(500, 300, moving, start + 730'000);
  assert(long_running.position() == moving);
  long_running.release(start + 750'000);
  long_running.tick(start + 1'000'000);
  assert(long_running.position() == far_pixels + width && long_running.settled() == far + 1);

  Swipe glide{width, 5, 2};
  assert(glide.advance(start, Swipe::Transition::Glide));
  glide.tick(start + 32'000);
  const auto first_glide_frame = glide.position();
  assert(first_glide_frame > 2 * width && first_glide_frame < 2 * width + width / 50);
  glide.tick(start + 320'000);
  assert(glide.position() == 2 * width + width / 2);
  glide.tick(start + 608'000);
  assert(3 * width - glide.position() == first_glide_frame - 2 * width);
  glide.tick(start + 640'000);
  assert(glide.idle() && glide.position() == 3 * width);

  // Slide Left uses the swipe position throughout the configured autoplay
  // transition. A tap resumes from the handed-off frame at the saved speed.
  for (const int64_t duration : {500'000, 1'000'000, 1'500'000}) {
    Swipe custom_glide{width, 5, 2};
    assert(custom_glide.advance(start, Swipe::Transition::Glide, duration));
    custom_glide.tick(start + duration / 2);
    const auto handoff = 2 * width + width / 2;
    assert(custom_glide.position() == handoff);
    custom_glide.tick(start + duration / 2 + 20'000);
    assert(custom_glide.position() > handoff);
    const auto pressed = start + duration / 2 + 21'000;
    custom_glide.press(540, 300, handoff, pressed);
    assert(custom_glide.position() == handoff);
    const auto released_at = pressed + 15'000;
    custom_glide.release(released_at);
    custom_glide.tick(released_at + duration / 4);
    assert(custom_glide.position() == handoff + width / 4);
    custom_glide.tick(released_at + duration / 2);
    assert(custom_glide.idle() && custom_glide.settled() == 3);
  }

  // Take over an actual presented frame midway through the ambient move.
  Swipe grab{width, 5, 2};
  assert(grab.advance(start, Swipe::Transition::Glide));
  grab.tick(start + 280'000);
  const auto frozen = grab.position();
  grab.press(540, 300, frozen, start + 290'000);
  grab.tick(start + 430'000);
  assert(grab.position() == frozen);
  grab.move(850, 300, start + 450'000);
  assert(grab.position() == frozen - 310);
  grab.release(start + 470'000);
  grab.tick(start + 700'000);
  assert(grab.position() == 2 * width && grab.idle());

  // A tap pauses and resumes the glide without converting it into a flick.
  Swipe tap{width, 5, 2};
  assert(tap.advance(start, Swipe::Transition::Glide));
  tap.tick(start + 320'000);
  const auto half = tap.position();
  tap.press(540, 300, half, start + 320'000);
  tap.release(start + 345'000);
  tap.tick(start + 540'000);
  assert(tap.settling() && tap.position() > half && tap.position() < 3 * width);
  tap.tick(start + 680'000);
  assert(tap.idle() && tap.position() == 3 * width);

  SlideDwell dwell;
  constexpr int64_t display_time = 2'000'000;
  assert(!dwell.elapsed(start, false, 0));
  assert(dwell.elapsed(start, true, 0));  // Start as soon as the first preview is ready.
  assert(!dwell.elapsed(start + 100'000, false, display_time));
  const auto full_frame = start + 640'000;
  assert(!dwell.elapsed(full_frame, true, display_time));
  assert(!dwell.elapsed(full_frame + display_time - 1, true, display_time));
  assert(dwell.progress_tenths_of_a_degree(full_frame + display_time / 2, display_time) == 1800);
  assert(dwell.elapsed(full_frame + display_time, true, display_time));
  assert(dwell.progress_tenths_of_a_degree(full_frame + display_time * 2, display_time) == 3600);
  assert(dwell.elapsed(full_frame + display_time * 2, true, display_time));  // Waiting for a download.
  assert(!dwell.elapsed(full_frame + display_time * 2, false, display_time));  // A touch.
  assert(dwell.progress_tenths_of_a_degree(full_frame + display_time * 2, display_time) == 0);
  const auto new_frame = full_frame + display_time * 2 + 1;
  assert(!dwell.elapsed(new_frame, true, display_time));
  assert(!dwell.elapsed(new_frame + 11'000'000, true, 15'000'000));
  assert(dwell.elapsed(new_frame + 11'000'000, true, 10'000'000));  // New interval, same photo.

  using espframe::native::ScreenRotation;
  using espframe::native::Viewport;
  std::vector<uint16_t> frame(espframe::native::pixel_count);
  for (auto rotation : {ScreenRotation::Normal, ScreenRotation::UpsideDown,
                        ScreenRotation::Clockwise, ScreenRotation::Counterclockwise}) {
    const Viewport view(rotation);
    const int ring_left = view.width - 24 - 32;
    const auto ring_pixel = [&](int x, int y) { return frame[view.offset(ring_left + x, 24 + y)]; };
    std::fill(frame.begin(), frame.end(), 0xffff);
    espframe::native::paint_slideshow_progress_ring(frame.data(), view, 900);
    assert(ring_pixel(16, 1) == 0xffff);  // The arc starts clockwise from the top.
    assert(ring_pixel(15, 1) == 0xce59);
    assert(ring_pixel(16, 30) == 0xce59);  // Black at 20% opacity over white.
    assert(ring_pixel(16, 16) == 0xffff);  // The centre remains transparent.
    std::fill(frame.begin(), frame.end(), 0xffff);
    espframe::native::paint_slideshow_progress_ring(frame.data(), view, 20);
    assert(ring_pixel(16, 1) > 0xce59 && ring_pixel(16, 1) < 0xffff);  // The advancing edge is blended.
  }

  const std::array<uint16_t, 6> left{0, 1, 2, 3, 4, 5};
  const std::array<uint16_t, 6> right{6, 7, 8, 9, 10, 11};
  std::array<uint16_t, 6> output{};
  espframe::native::blit_slides(output.data(), left.data(), nullptr, 2, 3, 0);
  assert(output == left);
  espframe::native::blit_slides(output.data(), left.data(), right.data(), 2, 3, 1);
  assert((output == std::array<uint16_t, 6>{2, 3, 4, 5, 6, 7}));
  espframe::native::blit_slides(output.data(), left.data(), right.data(), 2, 3, 2);
  assert((output == std::array<uint16_t, 6>{4, 5, 6, 7, 8, 9}));
  espframe::native::blit_slide_edge(output.data(), left.data(), 2, 3, -1);
  assert((output == std::array<uint16_t, 6>{0, 1, 0, 1, 2, 3}));
  espframe::native::blit_slide_edge(output.data(), left.data(), 2, 3, 1);
  assert((output == std::array<uint16_t, 6>{2, 3, 4, 5, 4, 5}));

  const std::array<uint16_t, 6> flipped_left{5, 4, 3, 2, 1, 0};
  const std::array<uint16_t, 6> flipped_right{11, 10, 9, 8, 7, 6};
  constexpr auto upside_down = espframe::native::ScreenRotation::UpsideDown;
  auto reference = output;
  for (int offset = 0; offset < 3; ++offset) {
    espframe::native::blit_slides(reference.data(), left.data(), right.data(), 2, 3, offset);
    const auto span = espframe::native::orient_slide_span(
        flipped_left.data(), offset ? flipped_right.data() : nullptr, 3, offset, upside_down);
    espframe::native::blit_slides(output.data(), span.first, span.second, 2, 3, span.offset);
    assert(std::equal(output.begin(), output.end(), reference.rbegin()));
  }
  for (const int pull : {-1, 1}) {
    espframe::native::blit_slide_edge(reference.data(), left.data(), 2, 3, pull);
    espframe::native::blit_slide_edge(output.data(), flipped_left.data(), 2, 3,
                                    pull * espframe::native::axis_direction(upside_down));
    assert(std::equal(output.begin(), output.end(), reference.rbegin()));
  }
}
