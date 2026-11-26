#include "autoplay_fade.h"
#include "frame_slots.h"
#include "swipe_input.h"

#include <cassert>

using namespace espframe::native;

int main() {
  AutoplayFade fade;
  constexpr int64_t start = 1'000'000;
  fade.start(2, 3, start);
  assert(fade.running() && fade.opacity(start) == 0);
  assert(fade.opacity(start + 50'000) < fade.opacity(start + 125'000));
  assert(fade.opacity(start + 250'000) == 127);
  assert(fade.opacity(start + 500'000) == AutoplayFade::opaque);

  // Touch before the first frame is submitted leaves the old photo as-is.
  assert(!fade.interrupt(false) && !fade.running() && !fade.held());
  fade.start(2, 3, start);
  fade.submitted(127);
  assert(fade.interrupt(false) && fade.held());
  fade.resume(start + 700'000);
  assert(fade.running() && fade.anchor() == 2 && fade.target() == 3);
  assert(fade.opacity(start + 700'000) == 0);
  assert(fade.opacity(start + 955'000) == AutoplayFade::opaque);

  // With no new mix submitted, another touch reuses the existing snapshot.
  assert(!fade.interrupt(true) && fade.held());
  Swipe swipe{slide_width, 5, 2};
  SwipeInput input;
  constexpr auto position = 2 * slide_width;
  input.update(swipe, {800, 400, 1, true, 1, start + 805'000}, position);
  input.update(swipe, {760, 400, 1, true, 2, start + 890'000}, position);
  assert(swipe.dragging() && swipe.position() == position + 40);
  fade.note_drag();
  input.update(swipe, {0, 0, 0, true, 3, start + 900'000}, position);
  swipe.tick(start + 1'200'000);
  assert(swipe.idle() && swipe.settled() == fade.anchor());
  fade.resume(start + 1'200'000);
  assert(fade.running() && fade.target() == fade.anchor());
  assert(fade.opacity(start + 1'330'000) == AutoplayFade::opaque);
  fade.finish();
  assert(!fade.running() && !fade.held());

  FrameSlots double_buffer;
  assert(!double_buffer.can_pin() && double_buffer.pin() == FrameSlots::none);
  assert(double_buffer.writable() == 1);
  double_buffer.submit(1);
  assert(double_buffer.writable() == FrameSlots::none);
  double_buffer.latch();
  assert(double_buffer.writable() == 0);

  FrameSlots triple_buffer{3};
  triple_buffer.submit(triple_buffer.writable());
  const auto fade_frame = triple_buffer.pin();
  assert(fade_frame == 1 && triple_buffer.writable() == FrameSlots::none);
  triple_buffer.latch();
  assert(triple_buffer.writable() == 0);
  triple_buffer.submit(0);
  triple_buffer.latch();
  assert(triple_buffer.writable() == 2);
  triple_buffer.submit(2);
  assert(triple_buffer.pin() == 2);  // A newer handoff replaces the old snapshot.
  triple_buffer.latch();
  triple_buffer.submit(triple_buffer.writable());
  triple_buffer.latch();
  assert(triple_buffer.writable() == 1);
  assert(triple_buffer.pin() == 0);
  triple_buffer.submit(1);
  triple_buffer.latch();
  assert(triple_buffer.writable() == 2);
  triple_buffer.unpin();
  assert(triple_buffer.writable() == 0);

  FrameSlots zoom_buffers{3};
  const auto scratch = zoom_buffers.reserve_preparation();
  assert(scratch == 1 && !zoom_buffers.can_submit(scratch));
  assert(zoom_buffers.reserve_preparation() == FrameSlots::none);
  const auto swipe_frame = zoom_buffers.writable();
  assert(swipe_frame == 2);
  zoom_buffers.submit(swipe_frame);
  assert(zoom_buffers.preparation_slot() == FrameSlots::none);
  zoom_buffers.latch();
  assert(zoom_buffers.writable() == 0);
  assert(zoom_buffers.release_preparation(scratch));
  assert(!zoom_buffers.release_preparation(scratch));
  zoom_buffers.submit(0);
  assert(zoom_buffers.preparation_slot() == scratch);
  zoom_buffers.latch();
  assert(zoom_buffers.can_submit(scratch));
}
