#include "preview_decoder.h"
#include "p4_jpeg_decoder.h"
#include "slide_geometry.h"
#include "esp_heap_caps.h"
#include "jpeg_accelerator_helpers.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

using namespace espframe::native;
using Decode = DecodedPreview::Result;

struct JpegDecoderEngine {};

namespace {

int hardware_calls;
int engine_creates;
bool dma_allocations_fail;
bool short_output;
bool force_timeout;
JpegDecoderEngine test_engine;

}  // namespace

int jpeg_new_decoder_engine(const jpeg_decode_engine_cfg_t *config, jpeg_decoder_handle_t *engine) {
  assert(config->timeout_ms > 0);
  ++engine_creates;
  *engine = &test_engine;
  return ESP_OK;
}

int jpeg_del_decoder_engine(jpeg_decoder_handle_t engine) {
  assert(engine == &test_engine);
  return ESP_OK;
}

void *jpeg_alloc_decoder_mem(size_t bytes, const jpeg_decode_memory_alloc_cfg_t *, size_t *capacity) {
  *capacity = bytes;
  return dma_allocations_fail ? nullptr : std::malloc(bytes);
}

int jpeg_decoder_process(jpeg_decoder_handle_t engine, const jpeg_decode_cfg_t *config, const uint8_t *encoded,
                         uint32_t length, uint8_t *output, uint32_t capacity, uint32_t *written) {
  assert(engine == &test_engine);
  assert(config->output_format == JPEG_DECODE_OUT_FORMAT_RGB565 &&
         config->rgb_order == JPEG_DEC_RGB_ELEMENT_ORDER_BGR && config->conv_std == JPEG_YUV_RGB_CONV_STD_BT601);
  ++hardware_calls;
  if (force_timeout)
    return ESP_ERR_TIMEOUT;

  esphome::remote_image::P4JpegInfo info;
  assert(esphome::remote_image::parse_p4_baseline_jpeg(encoded, length, &info));
  assert(((info.width == 30 && info.height == 32) || (info.width == 32 && info.height == 30)) &&
         info.padded_width == 32 && info.padded_height == 32);
  assert(capacity >= info.decoded_bytes);
  auto *pixels = reinterpret_cast<uint16_t *>(output);
  constexpr uint16_t colors[2][2]{{0xf800, 0x07e0}, {0x001f, 0xffff}};
  std::fill_n(pixels, info.decoded_bytes / sizeof(*pixels), 0xf81f);
  for (unsigned y = 0; y < info.height; ++y) {
    for (unsigned x = 0; x < info.width; ++x) {
      if (y >= 4 && y < info.height - 4)
        pixels[y * info.padded_width + x] = colors[y >= info.height / 2][x >= info.width / 2];
    }
  }
  *written = info.decoded_bytes - (short_output ? 2 : 0);
  return ESP_OK;
}

std::vector<uint8_t> load(const char *path) {
  std::ifstream file(path, std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
  assert(!bytes.empty());
  return bytes;
}

uint16_t at(const std::vector<uint16_t> &panel, int x, int y) {
  return panel[1 + x * panel_width + panel_width - 1 - y];
}

void expect_flipped(const std::vector<uint16_t> &upright, const DecodedPreview &photo,
                    const DecodedPreview *companion = nullptr, DisplayMode mode = DisplayMode::Fit) {
  const auto guard = upright.front();
  std::vector<uint16_t> flipped(pixel_count + 2, guard);
  photo.render(flipped.data() + 1, companion, mode, ScreenRotation::UpsideDown);
  assert(flipped.front() == guard && flipped.back() == guard);
  assert(std::equal(flipped.begin() + 1, flipped.end() - 1, upright.rbegin() + 1));
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
  expect_flipped(panel, decoded);
  assert(decoded.decode(photo.data(), photo.size(), false, nullptr, DisplayMode::Fill) == Decode::Ready);
  decoded.render(panel.data() + 1, nullptr, DisplayMode::Fill);
  assert(panel.front() == guard && panel.back() == guard);
  assert((at(panel, 0, 150) & 0xf800) >= 0xf000);
  assert((at(panel, slide_width - 1, 150) & 0x07e0) >= 0x07c0);
  assert((at(panel, 0, 650) & 0x001f) >= 0x001e);
  assert(at(panel, slide_width - 1, 650) >= 0xf7de);
  expect_flipped(panel, decoded, nullptr, DisplayMode::Fill);
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
  expect_flipped(panel, left, &right);
  assert(left.decode(primary.data(), primary.size(), false, nullptr, DisplayMode::Fill) == Decode::Ready);
  left.render(panel.data() + 1, nullptr, DisplayMode::Fill);
  assert(at(panel, 0, 420) == 0 && (at(panel, 640, 420) & 0xf800) >= 0xf000);
  assert(right.decode(companion.data(), companion.size(), true, nullptr, DisplayMode::Fill) == Decode::Ready);
  left.render(panel.data() + 1, &right, DisplayMode::Fill);
  assert((at(panel, 0, 420) & 0xf800) >= 0xf000 && (at(panel, 1279, 420) & 0x001f) >= 0x001e);
  assert(at(panel, 639, 420) == 0 && at(panel, 640, 420) == 0);
  expect_flipped(panel, left, &right, DisplayMode::Fill);
  assert(right.decode(companion.data(), companion.size() / 2, true) == Decode::Invalid);
  left.render(panel.data() + 1, &right);
  assert((at(panel, 640, 420) & 0xf800) >= 0xf000 && at(panel, 0, 420) == 0);
  fail_heap_allocations = true;
  assert(right.decode(companion.data(), companion.size(), true) == Decode::NoMemory);
  left.render(panel.data() + 1, &right);
  assert((at(panel, 640, 420) & 0xf800) >= 0xf000 && at(panel, 0, 420) == 0);
  fail_heap_allocations = false;
}

void verify_hardware(const char *baseline_path, const char *progressive_path, const char *webp_path) {
  auto baseline = load(baseline_path);
  const auto progressive = load(progressive_path);
  const auto webp = load(webp_path);
  // Two columns removed from the declared width leave the same MCU grid.
  constexpr std::array<uint8_t, 2> sof0{0xff, 0xc0};
  const auto sof = std::search(baseline.begin(), baseline.end(), sof0.begin(), sof0.end());
  assert(sof != baseline.end() && sof[7] == 0 && sof[8] == 32);
  sof[8] = 30;

  P4JpegDecoder hardware;
  DecodedPreview decoded;
  assert(decoded.decode(progressive.data(), progressive.size(), false, &hardware) == Decode::Ready);
  assert(!decoded.hardware_jpeg() && hardware_calls == 0);
  assert(decoded.decode(webp.data(), webp.size(), false, &hardware) == Decode::Ready);
  assert(!decoded.hardware_jpeg() && hardware_calls == 0);
  assert(decoded.decode(baseline.data(), baseline.size() / 2, false, &hardware) == Decode::Invalid);
  assert(!decoded.hardware_jpeg() && hardware_calls == 0);

  assert(decoded.decode(baseline.data(), baseline.size(), false, &hardware) == Decode::Ready);
  assert(decoded.hardware_jpeg() && hardware_calls == 1 && engine_creates == 1);
  constexpr uint16_t guard = 0x43cd;
  std::vector<uint16_t> panel(pixel_count + 2, guard);
  decoded.render(panel.data() + 1);
  assert(panel.front() == guard && panel.back() == guard);
  assert(at(panel, 264, 650) == 0 && at(panel, 1015, 650) == 0);
  assert(at(panel, 265, 150) == 0xf800 && at(panel, 1014, 150) == 0x07e0);
  assert(at(panel, 265, 650) == 0x001f && at(panel, 1014, 650) == 0xffff);
  expect_flipped(panel, decoded);
  sof[6] = 30;
  sof[8] = 32;
  assert(decoded.decode(baseline.data(), baseline.size(), false, &hardware, DisplayMode::Fill) == Decode::Ready);
  assert(decoded.hardware_jpeg() && hardware_calls == 2 && engine_creates == 1);
  decoded.render(panel.data() + 1, nullptr, DisplayMode::Fill);
  assert(at(panel, 0, 0) == 0xf800 && at(panel, slide_width - 1, slide_height - 1) == 0xffff);

  dma_allocations_fail = true;
  assert(decoded.decode(baseline.data(), baseline.size(), false, &hardware) == Decode::Ready);
  assert(!decoded.hardware_jpeg() && hardware_calls == 2);
  dma_allocations_fail = false;
  short_output = true;
  assert(decoded.decode(baseline.data(), baseline.size(), false, &hardware) == Decode::Ready);
  assert(!decoded.hardware_jpeg() && hardware_calls == 3 && engine_creates == 1);
  short_output = false;
  force_timeout = true;
  assert(decoded.decode(baseline.data(), baseline.size(), false, &hardware) == Decode::Ready);
  assert(!decoded.hardware_jpeg() && hardware_calls == 4);
  force_timeout = false;
  assert(decoded.decode(baseline.data(), baseline.size(), false, &hardware) == Decode::Ready);
  assert(!decoded.hardware_jpeg() && hardware_calls == 4 && engine_creates == 1);
}

int main(int argc, char **argv) {
  assert(argc == 6);
  const auto wide = frame_photo(1600, 900, slide_width, slide_height, DisplayMode::Fill);
  assert(wide.source.x == 80 && wide.source.y == 0 && wide.source.width == 1440 && wide.source.height == 900);
  const auto narrow = frame_photo(600, 500, slide_width, slide_height, DisplayMode::Fill);
  assert(narrow.source.x == 0 && narrow.source.y == 62 && narrow.source.width == 600 && narrow.source.height == 375);
  for (int i = 1; i < 4; ++i)
    verify(argv[i]);
  verify_pair(argv[4], argv[5]);
  verify_hardware(argv[1], argv[2], argv[3]);
  const uint8_t unsupported[]{0, 1, 2, 3};
  DecodedPreview decoded;
  assert(decoded.decode(unsupported, sizeof(unsupported)) == Decode::Invalid);
}
