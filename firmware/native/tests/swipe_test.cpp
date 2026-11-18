#include "slide_blit.h"
#include "swipe.h"

#include <array>
#include <cassert>

using espframe::native::Swipe;

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
  edge.release(start + 50'000);
  assert(edge.position() == 0 && !edge.settling());
  edge.press(300, 200, 0, start + 50'000);
  edge.move(280, 250, start + 100'000);
  edge.move(40, 250, start + 200'000);
  edge.release(start + 250'000);
  assert(edge.position() == 0 && !edge.settling());

  Swipe cold_boundary{width, 5, 2};
  cold_boundary.set_ready_range(1, 2);
  cold_boundary.press(950, 300, 2 * width, start);
  cold_boundary.move(300, 300, start + 80'000);
  assert(cold_boundary.position() == 2 * width);
  cold_boundary.set_ready_range(1, 3);
  cold_boundary.move(300, 300, start + 100'000);
  assert(cold_boundary.position() == 2 * width + 650);
  cold_boundary.release(start + 300'000);
  cold_boundary.tick(start + 600'000);
  assert(cold_boundary.settled() == 3);

  const std::array<uint16_t, 6> left{0, 1, 2, 3, 4, 5};
  const std::array<uint16_t, 6> right{6, 7, 8, 9, 10, 11};
  std::array<uint16_t, 6> output{};
  espframe::native::blit_slides(output.data(), left.data(), nullptr, 2, 3, 0);
  assert(output == left);
  espframe::native::blit_slides(output.data(), left.data(), right.data(), 2, 3, 1);
  assert((output == std::array<uint16_t, 6>{2, 3, 4, 5, 6, 7}));
  espframe::native::blit_slides(output.data(), left.data(), right.data(), 2, 3, 2);
  assert((output == std::array<uint16_t, 6>{4, 5, 6, 7, 8, 9}));
}
