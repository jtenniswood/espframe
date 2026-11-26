#include "photo_controls.h"
#include "photo_zoom_packets.h"

#include <cassert>
#include <vector>

using namespace espframe::native;

namespace {

int pixel_at(int x, int y, ScreenRotation rotation) {
  return panel_offset(x, slide_height - y - 1, rotation);
}

void draw(PhotoControls &controls, uint16_t *output, SlideSpan span, int pull, ScreenRotation rotation, int progress) {
  controls.prepare(output, span, pull, rotation, progress);
  controls.draw_rows(0, controls.split_row());
  controls.draw_rows(controls.split_row(), panel_height);
  controls.draw_toolbar();
}

constexpr int64_t test_refresh_us = 25'000;

int present(PhotoControls &controls, int64_t now) {
  const auto frame = controls.next_frame(now, test_refresh_us);
  controls.submitted(frame);
  controls.presented(now);
  return frame.progress;
}

void slows_down_near_both_ends() {
  PhotoControls controls;
  controls.animate(true, 0);
  std::array<int, 8> opening;
  for (size_t i = 0; i < opening.size(); ++i)
    opening[i] = present(controls, 50'001 + i * test_refresh_us);
  assert(opening[0] < PhotoControls::open / 16);
  assert(opening[3] == PhotoControls::open / 2);
  assert(opening[6] > 15 * PhotoControls::open / 16 && opening[6] < PhotoControls::open);
  assert(opening[7] == PhotoControls::open && !controls.animating());

  controls.animate(false, 250'000);
  std::array<int, 8> closing;
  for (size_t i = 0; i < closing.size(); ++i)
    closing[i] = present(controls, 250'001 + i * test_refresh_us);
  assert(closing[0] > 15 * PhotoControls::open / 16);
  assert(closing[3] == PhotoControls::open / 2);
  assert(closing[6] < PhotoControls::open / 16 && closing[6] > 0);
  assert(closing[7] == 0 && !controls.animating());
}

void text_only_fades_at_full_size() {
  PhotoControls controls;
  controls.animate(true, 0);
  assert(controls.text_opacity(0) == PhotoControls::opaque);
  const auto half = controls.text_opacity(25'000);
  assert(half > 0 && half < PhotoControls::opaque && controls.next_frame(25'000, test_refresh_us).progress == 0);
  assert(controls.text_opacity(50'000) == 0);
  for (int i = 0; i != 8; ++i)
    assert(present(controls, 50'001 + i * test_refresh_us) > 0 && controls.text_opacity(200'000) == 0);
  controls.animate(false, 250'000);
  for (int i = 0; i != 7; ++i)
    assert(present(controls, 250'001 + i * test_refresh_us) > 0);
  const auto full_size = controls.next_frame(500'000, test_refresh_us);
  assert(full_size.progress == 0);
  controls.submitted(full_size);
  assert(controls.text_opacity(550'000) == 0);
  controls.presented(550'000);
  assert(controls.text_opacity(590'000) == 127);
  assert(controls.text_opacity(630'000) == PhotoControls::opaque);

  controls.animate(true, 700'000, false);
  assert(controls.text_opacity(700'000) == 0);
  assert(controls.next_frame(700'001, test_refresh_us).progress > 0);
}

void tapping_again_during_the_fade_restores_text_without_resizing() {
  PhotoControls controls;
  controls.animate(true, 0);
  const auto faded = controls.text_opacity(20'000);
  controls.animate(false, 20'000);
  assert(controls.next_frame(20'000, test_refresh_us).progress == 0 && controls.text_opacity(20'000) == faded);
  assert(controls.next_frame(35'000, test_refresh_us).progress == 0 && controls.text_opacity(35'000) > faded);
  assert(controls.text_opacity(100'000) == PhotoControls::opaque);
}

void reverses_from_the_current_size_and_settles() {
  PhotoControls controls;
  controls.animate(true, 100);
  assert(controls.next_frame(100, test_refresh_us).progress == 0);
  present(controls, 50'101);
  present(controls, 75'101);
  const int middle = present(controls, 100'101);
  assert(middle > 0 && middle < PhotoControls::open);
  controls.animate(false, 120'100);
  assert(controls.next_frame(120'100, test_refresh_us).progress == middle);
  const int closing = present(controls, 150'100);
  assert(closing > 0 && closing < middle);
  controls.animate(true, 350'100);
  assert(controls.next_frame(350'100, test_refresh_us).progress == closing && controls.text_opacity(350'100) == 0);
  for (int i = 0; i != 8 && controls.animating(); ++i)
    present(controls, 350'101 + i * test_refresh_us);
  assert(!controls.animating() && controls.target_open());
  controls.reset();
  assert(!controls.target_open() && controls.next_frame(350'100, test_refresh_us).progress == 0);
  assert(controls.text_opacity(350'100) == PhotoControls::opaque);
}

void catches_up_after_a_slow_render() {
  PhotoControls controls;
  controls.animate(true, 100, false);
  const auto first = controls.next_frame(101, test_refresh_us);
  assert(first.elapsed_us == test_refresh_us);
  controls.submitted(first);
  const auto late = controls.next_frame(125'101, test_refresh_us);
  assert(late.elapsed_us == 6 * test_refresh_us);
  assert(late.progress > first.progress && late.progress < PhotoControls::open);
  controls.submitted(late);
  const auto open = controls.next_frame(230'100, test_refresh_us);
  assert(open.elapsed_us == 200'000 && open.progress == PhotoControls::open);
  controls.submitted(open);

  controls.animate(false, 250'100);
  const auto closing = controls.next_frame(325'101, test_refresh_us);
  assert(closing.elapsed_us == 4 * test_refresh_us && closing.progress == PhotoControls::open / 2);
  controls.submitted(closing);
  const auto closed = controls.next_frame(475'100, test_refresh_us);
  assert(closed.elapsed_us == 200'000 && closed.progress == 0);
}

void cached_steps_play_in_both_directions() {
  constexpr int64_t refresh_us = 19'243;
  static_assert(PhotoControls::supports_cached_steps(refresh_us));
  PhotoControls controls;
  constexpr int64_t start = 100'000;
  for (const bool opening : {true, false}) {
    const auto now = opening ? start : 2 * start + PhotoControls::resize_duration_us;
    controls.animate(opening, now, false);
    for (int step = 1; step <= PhotoControls::cached_steps; ++step) {
      const auto frame = controls.next_frame(now + step * refresh_us, refresh_us, true);
      const auto packet = opening ? step : PhotoControls::cached_steps - step;
      assert(frame.progress == PhotoControls::cached_progress(packet));
      assert(PhotoControls::cached_step(frame.progress) == packet);
      controls.submitted(frame);
    }
    assert(!controls.animating());
  }
}

void zoom_packets_are_aligned_and_only_exposed_when_complete() {
  PhotoZoomPackets clip;
  assert(!clip.ready() && !clip.append({}, PhotoZoomPackets::byte_limit));
  for (size_t step = 0; step < PhotoZoomPackets::frame_count; ++step) {
    const std::array<uint8_t, 5> packet{0xff, 0xd8, static_cast<uint8_t>(step), 0xff, 0xd9};
    assert(clip.frame(0).empty() && !clip.append(packet, PhotoZoomPackets::alignment - 1));
    fail_heap_allocations = true;
    assert(!clip.append(packet, PhotoZoomPackets::alignment));
    fail_heap_allocations = false;
    assert(clip.prepared_frames() == step);
    assert(clip.append(packet, PhotoZoomPackets::alignment));
  }
  assert(clip.ready() && clip.used_bytes() == PhotoZoomPackets::alignment * PhotoZoomPackets::frame_count);
  for (size_t step = PhotoZoomPackets::frame_count; step > 0; --step) {
    const auto packet = clip.frame(step - 1);
    assert(reinterpret_cast<uintptr_t>(packet.data()) % PhotoZoomPackets::alignment == 0);
    assert(packet[2] == step - 1);
  }
}

void keeps_both_halves_and_the_toolbar_in_the_same_place_when_rotated() {
  PhotoControls controls;
  constexpr uint16_t left = 0xF800;
  constexpr uint16_t right = 0x07E0;
  std::vector<uint16_t> photo(pixel_count, left);
  std::fill(photo.begin() + pixel_count / 2, photo.end(), right);
  std::vector<uint16_t> flipped(photo.rbegin(), photo.rend());
  std::vector<uint16_t> output(pixel_count);
  std::vector<uint16_t> rotated(pixel_count);
  constexpr auto normal = ScreenRotation::Normal;

  draw(controls, output.data(), {photo.data(), nullptr, 0}, 0, normal, 0);
  assert(output == photo);
  draw(controls, output.data(), {photo.data(), nullptr, 0}, 0, normal, 2);
  assert(output[pixel_at(639, 400, normal)] == left);
  assert(output[pixel_at(640, 400, normal)] == right);

  draw(controls, output.data(), {photo.data(), nullptr, 0}, 0, normal, PhotoControls::open);
  draw(controls, rotated.data(), {flipped.data(), nullptr, 0}, 0, ScreenRotation::UpsideDown, PhotoControls::open);
  assert(std::equal(output.begin(), output.end(), rotated.rbegin()));
  assert(output[pixel_at(639, 400, normal)] == left);
  assert(output[pixel_at(640, 400, normal)] == right);

  const auto surround = output[pixel_at(0, 0, normal)];
  assert(output[pixel_at(104, 104, normal)] == surround);
  assert(output[pixel_at(130, 106, normal)] == left);
  assert(output[pixel_at(505, 52, normal)] != surround);
  assert(output[pixel_at(505, 100, normal)] == surround);

  draw(controls, output.data(), {photo.data(), flipped.data(), 640}, 0, normal, PhotoControls::open);
  assert(output[pixel_at(400, 400, normal)] == right);
  assert(output[pixel_at(880, 400, normal)] == right);
  draw(controls, output.data(), {photo.data(), nullptr, 0}, 100, normal, PhotoControls::open);
  assert(output[pixel_at(600, 400, normal)] == right);
}

void scales_portrait_photos_through_the_swipe_seam() {
  PhotoControls controls;
  constexpr uint16_t red = 0xf800;
  constexpr uint16_t green = 0x07e0;
  const std::vector<uint16_t> first(pixel_count, red), second(pixel_count, green);
  std::vector<uint16_t> output(pixel_count);
  for (const auto rotation : {ScreenRotation::Clockwise, ScreenRotation::Counterclockwise}) {
    const Viewport viewport{rotation};
    const auto sample = [&](int x, int y) { return output[viewport.offset(x, y)]; };
    const auto span = orient_slide_span(first.data(), second.data(), viewport.width, viewport.width / 2, rotation);
    draw(controls, output.data(), span, 0, rotation, PhotoControls::open);
    const auto background = sample(0, 0);
    assert(sample(265, 52) != background && sample(265, 100) == background);
    assert(sample(130, 200) == red && sample(670, 200) == green);
    assert(sample(399, 500) == red && sample(400, 500) == green);
    assert(sample(0, 640) == background && sample(799, 640) == background);
    draw(controls, output.data(), {first.data(), nullptr, 0}, 60, rotation, PhotoControls::open);
    assert(sample(130, 200) == red && sample(670, 200) == red);
  }
}

void matches_pixel_center_sampling_in_each_orientation() {
  PhotoControls controls;
  std::vector<uint16_t> photo(pixel_count), output(pixel_count);
  for (int i = 0; i < pixel_count; ++i)
    photo[i] = 0xA000 | (i % panel_width);

  constexpr int row = panel_height / 2;
  for (const auto rotation : {ScreenRotation::Normal, ScreenRotation::UpsideDown, ScreenRotation::Clockwise,
                              ScreenRotation::Counterclockwise}) {
    const Viewport viewport{rotation};
    for (int progress = 0; progress <= PhotoControls::open; ++progress) {
      controls.prepare(output.data(), {photo.data(), nullptr, 0}, 0, rotation, progress);
      controls.draw_rows(row, row + 1);

      const auto scale = [&](int extent) { return (extent * progress + PhotoControls::open / 2) / PhotoControls::open; };
      const int left = scale(104 * viewport.width / slide_width);
      const int removed = scale(130 * viewport.height / slide_height);
      const int columns = viewport.portrait ? viewport.width - 2 * left : viewport.height - removed;
      const int top = removed * 4 / 5;
      const int logical_x = viewport.x_stride < 0 ? viewport.width - left - 1 : left;
      const int logical_y = viewport.y_stride < 0 ? top + viewport.height - removed - 1 : top;
      const int first_column = viewport.offset(logical_x, logical_y) % panel_width;
      for (int column = 0; column < columns; ++column) {
        const int logical = oriented_axis(column, columns, rotation);
        const int sampled = oriented_axis((2 * logical + 1) * panel_width / (2 * columns), panel_width, rotation);
        assert(output[row * panel_width + first_column + column] == (0xA000 | sampled));
      }
    }
  }
}

}  // namespace

int main() {
  slows_down_near_both_ends();
  text_only_fades_at_full_size();
  tapping_again_during_the_fade_restores_text_without_resizing();
  reverses_from_the_current_size_and_settles();
  catches_up_after_a_slow_render();
  cached_steps_play_in_both_directions();
  zoom_packets_are_aligned_and_only_exposed_when_complete();
  keeps_both_halves_and_the_toolbar_in_the_same_place_when_rotated();
  scales_portrait_photos_through_the_swipe_seam();
  matches_pixel_center_sampling_in_each_orientation();
}
