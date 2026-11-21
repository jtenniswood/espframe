#include "clock_overlay.h"
#include "slide_blit.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

using namespace espframe::native;

void verify_upside_down_clock(const ClockSettings &settings, const std::vector<uint8_t> &atlas,
                             std::time_t time) {
  std::vector<uint16_t> photo(pixel_count);
  for (int pixel = 0; pixel < pixel_count; ++pixel)
    photo[pixel] = static_cast<uint16_t>(pixel * 3);
  const std::vector<uint16_t> flipped_photo(photo.rbegin(), photo.rend());

  ClockOverlay upright;
  ClockOverlay upside_down;
  assert(upright.init(settings, atlas.data(), true));
  constexpr auto rotation = ScreenRotation::UpsideDown;
  assert(upside_down.init(settings, atlas.data(), true, rotation));
  assert(upright.update(local_clock_time(time)) && upside_down.update(local_clock_time(time)));
  auto held = photo;
  auto flipped_held = flipped_photo;
  upright.draw(held.data(), true);
  upside_down.draw(flipped_held.data(), true);
  assert(std::equal(held.rbegin(), held.rend(), flipped_held.begin()));
  upright.pin_underlay();
  upside_down.pin_underlay();

  std::vector<uint16_t> expected(pixel_count);
  std::vector<uint16_t> actual(pixel_count);
  for (const int offset : {70, panel_height - 70}) {
    // Use the held frame on both sides of the seam to exercise both copy
    // ranges for the original clock patch.
    for (const bool held_on_left : {false, true}) {
      blit_slides(expected.data(), photo.data(), photo.data(), panel_width, panel_height, offset);
      const auto *left = held_on_left ? flipped_held.data() : flipped_photo.data();
      const auto *right = held_on_left ? flipped_photo.data() : flipped_held.data();
      const auto span = orient_slide_span(left, right, panel_height, offset, rotation);
      blit_slides(actual.data(), span.first, span.second, panel_width, panel_height, span.offset);
      if (span.first == flipped_held.data())
        upside_down.restore_rows(actual.data(), span.offset, 0, panel_height - span.offset);
      if (span.second == flipped_held.data())
        upside_down.restore_rows(actual.data(), 0, panel_height - span.offset, span.offset);
      assert(std::equal(expected.rbegin(), expected.rend(), actual.begin()));
    }
  }

  for (const int logical_pull : {-40, 40}) {
    const int pull = logical_pull * axis_direction(rotation);
    blit_slide_edge(expected.data(), photo.data(), panel_width, panel_height, logical_pull);
    blit_slide_edge(actual.data(), flipped_held.data(), panel_width, panel_height, pull);
    upside_down.restore_rows(actual.data(), std::max(0, pull), std::max(0, -pull), panel_height - std::abs(pull));
    assert(std::equal(expected.rbegin(), expected.rend(), actual.begin()));
  }

  std::fill(expected.begin(), expected.end(), 0);
  std::fill(actual.begin(), actual.end(), 0);
  upright.restore_blend(expected.data(), photo.data(), 128);
  upside_down.restore_blend(actual.data(), flipped_photo.data(), 128);
  assert(std::equal(expected.rbegin(), expected.rend(), actual.begin()));
}

void expect_glyph(const std::vector<uint16_t> &frame, const std::vector<uint8_t> &atlas, int place, int glyph) {
  constexpr int width = ClockOverlay::glyph_width;
  constexpr int height = ClockOverlay::glyph_height;
  bool found_opaque = false;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto value = atlas[(glyph * width * height + x * height + y) / 2];
      if ((y % 2 != 0 ? value & 15 : value >> 4) == 15) {
        const int row = ClockOverlay::first_row + place * ClockOverlay::digit_advance + x -
                        (place > 2 ? ClockOverlay::digit_advance - ClockOverlay::colon_advance : 0);
        const int column = ClockOverlay::first_column + ClockOverlay::columns - 1 - y;
        assert(frame[row * panel_width + column] == 0xffff);
        found_opaque = true;
      }
    }
  }
  assert(found_opaque);
}

void verify_antialiasing() {
  std::vector<uint8_t> atlas(ClockOverlay::atlas_size);
  // Two different coverage values in the first column of the glyph for 1.
  atlas[ClockOverlay::glyph_width * ClockOverlay::glyph_height / 2] = 0x87;
  ClockSettings settings;
  settings.show = true;
  ClockOverlay clock;
  assert(clock.init(settings, atlas.data(), false));
  std::tm time{};
  time.tm_hour = 13;
  time.tm_min = 34;
  assert(clock.update(time));
  std::vector<uint16_t> frame(pixel_count, 0x39e7);
  clock.draw(frame.data());
  constexpr int top = ClockOverlay::first_column + ClockOverlay::columns - 1;
  const auto pixel = [&](int x, int y) {
    return frame[(ClockOverlay::first_row + x) * panel_width + top - y];
  };
  assert(pixel(0, 0) == 0xa534 && pixel(0, 1) == 0x94b2);
  assert(pixel(2, 2) == 0x31a6 && pixel(2, 3) == 0x31c6);
  assert(pixel(0, 2) == 0x39e7 && pixel(2, 4) == 0x39e7);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  verify_antialiasing();
  std::ifstream file(argv[1], std::ios::binary);
  const std::vector<uint8_t> atlas{std::istreambuf_iterator<char>(file), {}};
  assert(atlas.size() == ClockOverlay::atlas_size);
  static constexpr uint16_t photo = 0x39e7;
  const std::vector<uint16_t> plain(pixel_count, photo);
  auto baked = plain;
  auto moved = plain;

  ClockSettings settings;
  settings.show = true;
  assert(configure_local_time(settings.timezone));
  ClockOverlay clock;
  assert(clock.init(settings, atlas.data(), true));
  assert(!clock.update(local_clock_time(0)));
  clock.draw(baked.data(), true);
  assert(baked == plain);

  // 12:34 UTC on 1 July is 13:34 in Europe/London.
  constexpr std::time_t summer = 1'782'909'240;
  assert(clock.update(local_clock_time(summer)) && !clock.update(local_clock_time(summer + 1)));
  verify_upside_down_clock(settings, atlas, summer);
  assert(local_clock_time(summer)->tm_hour == 13);
  clock.draw(baked.data(), true);
  expect_glyph(baked, atlas, 0, 1);
  expect_glyph(baked, atlas, 1, 3);
  expect_glyph(baked, atlas, 2, 10);
  expect_glyph(baked, atlas, 3, 3);
  expect_glyph(baked, atlas, 4, 4);
  assert(baked.front() == photo && baked.back() == photo);
  clock.pin_underlay();
  constexpr int shift = 80;
  blit_slides(moved.data(), baked.data(), plain.data(), panel_width, panel_height, shift);
  assert(moved != plain);
  clock.restore_rows(moved.data(), shift, 0, panel_height - shift);
  assert(moved == plain);
  blit_slides(moved.data(), plain.data(), baked.data(), panel_width, panel_height, shift);
  assert(moved != plain);
  clock.restore_rows(moved.data(), 0, panel_height - shift, shift);
  assert(moved == plain);
  clock.draw(moved.data());
  assert(moved == baked);

  // A resumed crossfade reconstructs the unpainted patch from the held
  // photo and the new target, so its old clock cannot show through.
  const std::vector<uint16_t> white(pixel_count, 0xffff);
  clock.restore_blend(moved.data(), white.data(), 128);
  for (int x = ClockOverlay::first_row; x < ClockOverlay::first_row + ClockOverlay::rows; ++x) {
    for (int y = ClockOverlay::first_column; y < ClockOverlay::first_column + ClockOverlay::columns; ++y)
      assert(moved[x * panel_width + y] == 0x9cf3);
  }
  assert(moved.front() == photo && moved.back() == photo);

  settings.format = "12 Hour";
  ClockOverlay twelve_hour;
  assert(twelve_hour.init(settings, atlas.data(), false));
  constexpr std::time_t winter = 1'767'225'900;  // 00:05 UTC in London in winter.
  assert(local_clock_time(winter)->tm_hour == 0);
  assert(twelve_hour.update(local_clock_time(winter)));
  auto midnight = plain;
  twelve_hour.draw(midnight.data());
  expect_glyph(midnight, atlas, 0, 1);
  expect_glyph(midnight, atlas, 1, 2);
  expect_glyph(midnight, atlas, 2, 10);
  expect_glyph(midnight, atlas, 3, 0);
  expect_glyph(midnight, atlas, 4, 5);

  twelve_hour.set_twelve_hour(false);
  assert(twelve_hour.update(local_clock_time(winter)));
  auto reformatted = plain;
  twelve_hour.draw(reformatted.data());
  expect_glyph(reformatted, atlas, 0, 0);
  expect_glyph(reformatted, atlas, 1, 0);
  expect_glyph(reformatted, atlas, 4, 5);
  assert(reformatted != midnight && !twelve_hour.update(local_clock_time(winter)));
}
