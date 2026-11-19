#include "clock_overlay.h"
#include "slide_blit.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

using namespace espframe::native;

void expect_glyph(const std::vector<uint16_t> &frame, const std::vector<uint8_t> &atlas, int place, int glyph) {
  constexpr int width = ClockOverlay::glyph_width;
  constexpr int height = ClockOverlay::glyph_height;
  bool found_opaque = false;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto value = atlas[(glyph * width * height + y * width + x) / 2];
      if ((x % 2 != 0 ? value & 15 : value >> 4) == 15) {
        const int row = ClockOverlay::first_row + place * width + x - (place > 2 ? width / 2 : 0);
        const int column = ClockOverlay::first_column + ClockOverlay::columns - 1 - y;
        assert(frame[row * panel_width + column] == 0xffff);
        found_opaque = true;
      }
    }
  }
  assert(found_opaque);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  std::ifstream file(argv[1], std::ios::binary);
  const std::vector<uint8_t> atlas{std::istreambuf_iterator<char>(file), {}};
  assert(atlas.size() == ClockOverlay::atlas_size);
  static constexpr uint16_t photo = 0x39e7;
  const std::vector<uint16_t> plain(pixel_count, photo);
  auto baked = plain;
  auto moved = plain;

  ClockSettings settings;
  settings.show = true;
  ClockOverlay clock;
  assert(clock.init(settings, atlas.data(), true));
  assert(!clock.update(0));
  clock.draw(baked.data(), true);
  assert(baked == plain);

  // 12:34 UTC on 1 July is 13:34 in Europe/London.
  constexpr std::time_t summer = 1'782'909'240;
  assert(clock.update(summer) && !clock.update(summer + 1));
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
  assert(twelve_hour.update(1'767'225'900));  // 00:05 UTC in London in winter.
  auto midnight = plain;
  twelve_hour.draw(midnight.data());
  expect_glyph(midnight, atlas, 0, 1);
  expect_glyph(midnight, atlas, 1, 2);
  expect_glyph(midnight, atlas, 2, 10);
  expect_glyph(midnight, atlas, 3, 0);
  expect_glyph(midnight, atlas, 4, 5);
}
