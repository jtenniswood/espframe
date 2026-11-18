#include "preview_decoder.h"
#include "slide_geometry.h"
#include "esp_heap_caps.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

using namespace espframe::native;
using Decode = DecodedPreview::Result;

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
  DecodedPreview decoded;
  constexpr uint16_t guard = 0x43cd;
  std::vector<uint16_t> panel(pixel_count + 2, guard);
  assert(decoded.decode(photo.data(), photo.size()) == Decode::Ready);
  decoded.render(panel.data() + 1);
  assert(panel.front() == guard && panel.back() == guard);
  // The 1:1 photo is centered. Four distinct colors catch a wrong direction
  // of rotation, row/column confusion, and byte-swapped RGB565.
  assert(at(panel, 100, 400) == 0 && at(panel, 1180, 400) == 0);
  assert((at(panel, 400, 150) & 0xf800) >= 0xf000);
  assert((at(panel, 880, 150) & 0x07e0) >= 0x07c0);
  assert((at(panel, 400, 650) & 0x001f) >= 0x001e);
  assert(at(panel, 880, 650) >= 0xf7de);
  assert(decoded.decode(photo.data(), photo.size() / 2) == Decode::Invalid);
}

void verify_pair(const char *primary_path, const char *companion_path) {
  DecodedPreview left;
  DecodedPreview right;
  const auto primary = load(primary_path);
  const auto companion = load(companion_path);
  assert(left.decode(primary.data(), primary.size()) == Decode::Ready && left.portrait());
  assert(right.decode(companion.data(), companion.size(), true) == Decode::Ready && right.portrait());
  constexpr uint16_t guard = 0x43cd;
  std::vector<uint16_t> panel(pixel_count + 2, guard);
  left.render(panel.data() + 1, &right);
  assert(panel.front() == guard && panel.back() == guard);
  assert((at(panel, 320, 420) & 0xf800) >= 0xf000);
  assert((at(panel, 960, 420) & 0x001f) >= 0x001e);
  assert(at(panel, 639, 420) == 0 && at(panel, 640, 420) == 0);
  assert(at(panel, 320, 0) == 0 && at(panel, 960, 0) != 0);
  assert(right.decode(companion.data(), companion.size() / 2, true) == Decode::Invalid);
  left.render(panel.data() + 1, &right);
  assert((at(panel, 640, 420) & 0xf800) >= 0xf000 && at(panel, 0, 420) == 0);
  fail_heap_allocations = true;
  assert(right.decode(companion.data(), companion.size(), true) == Decode::NoMemory);
  left.render(panel.data() + 1, &right);
  assert((at(panel, 640, 420) & 0xf800) >= 0xf000 && at(panel, 0, 420) == 0);
  fail_heap_allocations = false;
}

int main(int argc, char **argv) {
  assert(argc == 6);
  for (int i = 1; i < 4; ++i)
    verify(argv[i]);
  verify_pair(argv[4], argv[5]);
  const uint8_t unsupported[]{0, 1, 2, 3};
  DecodedPreview decoded;
  assert(decoded.decode(unsupported, sizeof(unsupported)) == Decode::Invalid);
}
