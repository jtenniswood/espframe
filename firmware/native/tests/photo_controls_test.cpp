#include "photo_controls.h"

#include <cassert>
#include <vector>

using namespace espframe::native;

namespace {

int pixel_at(int x, int y, ScreenRotation rotation) {
  return panel_offset(x, slide_height - y - 1, rotation);
}

void draw(PhotoControls &controls, uint16_t *output, SlideSpan span, int pull, ScreenRotation rotation, int progress) {
  controls.prepare(output, span, pull, rotation, progress);
  controls.draw_rows(0, panel_height / 2);
  controls.draw_rows(panel_height / 2, panel_height);
  controls.draw_toolbar();
}

void slows_down_near_both_ends() {
  PhotoControls controls;
  controls.animate(true, 0);
  assert(controls.progress(75'000) < PhotoControls::open / 16);
  assert(controls.progress(150'000) == PhotoControls::open / 2);
  assert(controls.progress(225'000) > 15 * PhotoControls::open / 16);
  assert(controls.progress(249'999) < PhotoControls::open);
  assert(controls.progress(250'000) == PhotoControls::open);

  controls.animate(false, 250'000);
  assert(controls.progress(275'000) > 15 * PhotoControls::open / 16);
  assert(controls.progress(350'000) == PhotoControls::open / 2);
  assert(controls.progress(425'000) < PhotoControls::open / 16);
  assert(controls.progress(449'999) > 0);
  assert(controls.progress(450'000) == 0);
}

void text_only_fades_at_full_size() {
  PhotoControls controls;
  controls.animate(true, 0);
  assert(controls.text_opacity(0) == PhotoControls::opaque);
  const auto half = controls.text_opacity(25'000);
  assert(half > 0 && half < PhotoControls::opaque && controls.progress(25'000) == 0);
  assert(controls.text_opacity(50'000) == 0);
  assert(controls.text_opacity(200'000) == 0 && controls.progress(200'000) > 0);
  controls.animate(false, 250'000);
  assert(controls.text_opacity(449'999) == 0);
  assert(controls.progress(450'000) == 0 && controls.text_opacity(450'000) == 0);
  assert(controls.text_opacity(490'000) == 127);
  assert(controls.text_opacity(530'000) == PhotoControls::opaque);

  controls.animate(true, 600'000, false);
  assert(controls.text_opacity(600'000) == 0);
  assert(controls.progress(700'000) == PhotoControls::open / 2);
}

void tapping_again_during_the_fade_restores_text_without_resizing() {
  PhotoControls controls;
  controls.animate(true, 0);
  const auto faded = controls.text_opacity(20'000);
  controls.animate(false, 20'000);
  assert(controls.progress(20'000) == 0 && controls.text_opacity(20'000) == faded);
  assert(controls.progress(35'000) == 0 && controls.text_opacity(35'000) > faded);
  assert(controls.text_opacity(100'000) == PhotoControls::opaque);
}

void reverses_from_the_current_size_and_settles() {
  PhotoControls controls;
  controls.animate(true, 100);
  assert(controls.progress(100) == 0);
  const int middle = controls.progress(120'100);
  assert(middle > 0 && middle < PhotoControls::open);
  controls.animate(false, 120'100);
  assert(controls.progress(120'100) == middle);
  const int closing = controls.progress(150'100);
  assert(closing > 0 && closing < middle);
  controls.animate(true, 150'100);
  assert(controls.progress(150'100) == closing && controls.text_opacity(150'100) == 0);
  assert(controls.progress(350'100) == PhotoControls::open);
  controls.reset();
  assert(!controls.target_open() && controls.progress(350'100) == 0);
  assert(controls.text_opacity(350'100) == PhotoControls::opaque);
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

}  // namespace

int main() {
  slows_down_near_both_ends();
  text_only_fades_at_full_size();
  tapping_again_during_the_fade_restores_text_without_resizing();
  reverses_from_the_current_size_and_settles();
  keeps_both_halves_and_the_toolbar_in_the_same_place_when_rotated();
  scales_portrait_photos_through_the_swipe_seam();
}
