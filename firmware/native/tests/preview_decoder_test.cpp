#include "preview_decoder.h"
#include "slide_geometry.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

using namespace espframe::native;

std::vector<uint8_t> load(const char *path) {
  std::ifstream file(path, std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
  assert(!bytes.empty());
  return bytes;
}

uint16_t at(const std::vector<uint16_t> &panel, int x, int y) {
  return panel[1 + x * panel_width + panel_width - 1 - y];
}

void verify(const char *path) {
  const auto photo = load(path);
  constexpr uint16_t guard = 0x43cd;
  std::vector<uint16_t> panel(pixel_count + 2, guard);
  assert(decode_preview(photo.data(), photo.size(), panel.data() + 1));
  assert(panel.front() == guard && panel.back() == guard);
  // The 1:1 photo is centered. Four distinct colors catch a wrong direction
  // of rotation, row/column confusion, and byte-swapped RGB565.
  assert(at(panel, 100, 400) == 0 && at(panel, 1180, 400) == 0);
  assert((at(panel, 400, 150) & 0xf800) >= 0xf000);
  assert((at(panel, 880, 150) & 0x07e0) >= 0x07c0);
  assert((at(panel, 400, 650) & 0x001f) >= 0x001e);
  assert(at(panel, 880, 650) >= 0xf7de);
  assert(!decode_preview(photo.data(), photo.size() / 2, panel.data() + 1));
}

int main(int argc, char **argv) {
  assert(argc == 4);
  for (int i = 1; i < argc; ++i)
    verify(argv[i]);
  const uint8_t unsupported[]{0, 1, 2, 3};
  std::vector<uint16_t> panel(pixel_count);
  assert(!decode_preview(unsupported, sizeof(unsupported), panel.data()));
}
