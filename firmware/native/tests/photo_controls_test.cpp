#include "photo_controls.h"

#include <cassert>
#include <vector>

using namespace espframe::native;

namespace {

int pixel_at(int x, int y, ScreenRotation rotation) {
  return panel_offset(x, slide_height - y - 1, rotation);
}

void reverses_from_the_current_size_and_settles() {
  PhotoControls controls;
  controls.animate(true, 100);
  assert(controls.progress(100) == 0);
  const int middle = controls.progress(70'100);
  assert(middle > 0 && middle < PhotoControls::open);
  controls.animate(false, 70'100);
  assert(controls.progress(70'100) == middle);
  const int closing = controls.progress(100'100);
  assert(closing > 0 && closing < middle);
  controls.animate(true, 100'100);
  assert(controls.progress(100'100) == closing);
  assert(controls.progress(430'100) == PhotoControls::open);
  controls.reset();
  assert(!controls.target_open() && controls.progress(430'100) == 0);
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

  controls.draw(output.data(), {photo.data(), nullptr, 0}, 0, normal, 0);
  assert(output == photo);
  controls.draw(output.data(), {photo.data(), nullptr, 0}, 0, normal, 2);
  assert(output[pixel_at(639, 400, normal)] == left);
  assert(output[pixel_at(640, 400, normal)] == right);

  controls.draw(output.data(), {photo.data(), nullptr, 0}, 0, normal, PhotoControls::open);
  controls.draw(rotated.data(), {flipped.data(), nullptr, 0}, 0, ScreenRotation::UpsideDown, PhotoControls::open);
  assert(std::equal(output.begin(), output.end(), rotated.rbegin()));
  assert(output[pixel_at(639, 400, normal)] == left);
  assert(output[pixel_at(640, 400, normal)] == right);

  const auto surround = output[pixel_at(0, 0, normal)];
  assert(output[pixel_at(104, 104, normal)] == surround);
  assert(output[pixel_at(130, 106, normal)] == left);
  assert(output[pixel_at(505, 52, normal)] != surround);
  assert(output[pixel_at(505, 100, normal)] == surround);

  controls.draw(output.data(), {photo.data(), flipped.data(), 640}, 0, normal, PhotoControls::open);
  assert(output[pixel_at(400, 400, normal)] == right);
  assert(output[pixel_at(880, 400, normal)] == right);
  controls.draw(output.data(), {photo.data(), nullptr, 0}, 100, normal, PhotoControls::open);
  assert(output[pixel_at(600, 400, normal)] == right);
}

}  // namespace

int main() {
  reverses_from_the_current_size_and_settles();
  keeps_both_halves_and_the_toolbar_in_the_same_place_when_rotated();
}
