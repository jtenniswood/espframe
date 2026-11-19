#include "slide_blit.h"
#include "slide_dwell.h"
#include "swipe.h"

#include <array>
#include <cassert>

using espframe::native::Swipe;
using espframe::native::SlideIndex;
using espframe::native::PixelPosition;
using espframe::native::SlideDwell;

int main() {
  constexpr int width = 1280;
  constexpr int64_t start = 1'000'000;

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

  Swipe previous_waiting{width, 5, 2};
  previous_waiting.set_ready_range(2, 3);
  previous_waiting.press(150, 300, 2 * width, start);
  previous_waiting.move(800, 300, start + 80'000);
  const auto resisted_previous = previous_waiting.position();
  assert(resisted_previous < 2 * width && resisted_previous > 2 * width - 48);
  previous_waiting.set_ready_range(1, 3);
  previous_waiting.move(800, 300, start + 100'000);
  assert(previous_waiting.position() == resisted_previous);
  previous_waiting.move(835, 300, start + 110'000);
  assert(previous_waiting.position() == resisted_previous - 35);
  previous_waiting.release(start + 120'000);
  previous_waiting.tick(start + 600'000);
  assert(previous_waiting.settled() == 1);

  // Hold the visible edge until the missed photo can join the transition.
  Swipe waiting{width, 5, 2};
  waiting.set_ready_range(1, 2);
  waiting.press(900, 400, 2 * width, start);
  waiting.move(100, 400, start + 50'000);
  assert(waiting.position() > 2 * width && waiting.position() < 2 * width + 48);
  constexpr int64_t released = start + 55'000;
  waiting.release(released);
  const auto held_edge = waiting.position();
  waiting.tick(released + 120'000);
  assert(waiting.position() == held_edge && !waiting.idle() && !waiting.advance(released + 120'000, Swipe::Transition::Glide));
  assert(waiting.loading_direction(released + 159'999) == 0);
  assert(waiting.loading_direction(released + 160'000) == 1);
  waiting.set_ready_range(1, 3);
  waiting.tick(released + 280'000);
  assert(waiting.settling() && waiting.position() == held_edge && waiting.loading_direction(released + 280'000) == 0);
  waiting.tick(released + 600'000);
  assert(waiting.idle() && waiting.settled() == 3);

  Swipe expired{width, 5, 2};
  expired.set_ready_range(2, 3);
  expired.press(200, 400, 2 * width, start);
  expired.move(900, 400, start + 50'000);
  expired.release(released);
  assert(expired.loading_direction(released + 160'000) == -1);
  const auto deadline = released + Swipe::max_loading_wait_us;
  expired.tick(deadline - 1);
  assert(!expired.idle() && !expired.settling());
  expired.set_ready_range(1, 3);
  expired.tick(deadline);
  expired.tick(deadline + 250'000);
  assert(expired.idle() && expired.settled() == 2);

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

  // An evicted history position is a true end, even when the panel has pixels for a newer neighbour.
  Swipe oldest{width, 5, 2};
  oldest.set_ready_range(2, 3);
  oldest.set_available_range(2, 4);
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
  assert(dwell.elapsed(full_frame + display_time, true, display_time));
  assert(dwell.elapsed(full_frame + display_time * 2, true, display_time));  // Waiting for a download.
  assert(!dwell.elapsed(full_frame + display_time * 2, false, display_time));  // A touch.
  assert(!dwell.elapsed(full_frame + display_time * 2 + 1, true, display_time));

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
