#include "preview_decoder.h"
#include "p4_jpeg_decoder.h"
#include "recent_previews.h"
#include "slide_geometry.h"
#include "trial_pair_fill.h"
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
  assert(((info.width == 30 && info.height == 32) || (info.width == 32 && info.height == 30) ||
          (info.width == 32 && info.height == 32)) &&
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
  std::fill(flipped.begin() + 1, flipped.end() - 1, static_cast<uint16_t>(~guard));
  assert(photo.render(flipped.data() + 1, companion, mode, ScreenRotation::UpsideDown));
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
  assert(decoded.decode(photo.data(), photo.size(), {PreviewTarget::Slide, DisplayMode::Fill}) == Decode::Ready);
  decoded.render(panel.data() + 1, nullptr, DisplayMode::Fill);
  assert(panel.front() == guard && panel.back() == guard);
  assert((at(panel, 0, 150) & 0xf800) >= 0xf000);
  assert((at(panel, slide_width - 1, 150) & 0x07e0) >= 0x07c0);
  assert((at(panel, 0, 650) & 0x001f) >= 0x001e);
  assert(at(panel, slide_width - 1, 650) >= 0xf7de);
  expect_flipped(panel, decoded, nullptr, DisplayMode::Fill);
  for (const auto mode : {DisplayMode::Fit, DisplayMode::Fill}) {
    for (const auto rotation : {ScreenRotation::Clockwise, ScreenRotation::Counterclockwise}) {
      const Viewport viewport{rotation};
      assert(decoded.decode(photo.data(), photo.size(), {PreviewTarget::Slide, mode, mode, rotation}) == Decode::Ready);
      std::fill(panel.begin() + 1, panel.end() - 1, guard);
      assert(decoded.render(panel.data() + 1, nullptr, mode, rotation));
      const auto pixel = [&](int x, int y) { return panel[1 + viewport.offset(x, y)]; };
      const auto framing = frame_photo(32, 32, viewport.width, viewport.height, mode);
      const int top = (viewport.height - framing.height) / 2;
      const int upper = top + framing.height / 4;
      const int lower = top + 3 * framing.height / 4;
      assert(panel.front() == guard && panel.back() == guard);
      assert((pixel(100, upper) & 0xf800) >= 0xf000 && (pixel(700, upper) & 0x07e0) >= 0x07c0);
      assert((pixel(100, lower) & 0x001f) >= 0x001e && pixel(700, lower) >= 0xf7de);
      assert((pixel(400, 0) == 0) == (mode == DisplayMode::Fit));
    }
  }
  assert(decoded.decode(photo.data(), photo.size() / 2) == Decode::Invalid);
}

void verify_pair(const char *primary_path, const char *companion_path) {
  DecodedPreview left;
  DecodedPreview right;
  const auto primary = load(primary_path);
  const auto companion = load(companion_path);
  assert(left.decode(primary.data(), primary.size()) == Decode::Ready && left.portrait());
  assert(right.decode(companion.data(), companion.size(), {PreviewTarget::PairPane}) == Decode::Ready && right.portrait());
  constexpr uint16_t guard = 0x43cd;
  std::vector<uint16_t> panel(pixel_count + 2, guard);
  left.render(panel.data() + 1, &right);
  assert(panel.front() == guard && panel.back() == guard);
  assert((at(panel, 320, 420) & 0xf800) >= 0xf000);
  assert((at(panel, 960, 420) & 0x001f) >= 0x001e);
  assert((at(panel, 639, 420) & 0xf800) >= 0xf000 && (at(panel, 640, 420) & 0x001f) >= 0x001e);
  assert(at(panel, slide_width - 1, 420) == 0);
  assert(at(panel, 320, 0) == 0 && at(panel, 960, 0) != 0);
  assert(!replay_pair_fills_panes(panel.data() + 1, ScreenRotation::Normal));
  const auto full_photo_pair = panel;
  expect_flipped(panel, left, &right);
  assert(left.decode(primary.data(), primary.size(), {PreviewTarget::Slide, DisplayMode::Fill}) == Decode::Ready);
  left.render(panel.data() + 1);
  assert((at(panel, 0, 420) & 0xf800) >= 0xf000 && (at(panel, 640, 420) & 0xf800) >= 0xf000);
  expect_flipped(panel, left, nullptr);

  // A flexible primary keeps the full portrait for Fit pairs, even when
  // a single-photo fallback would fill the whole frame.
  assert(left.decode(primary.data(), primary.size(), {PreviewTarget::OptionalPair, DisplayMode::Fill}) == Decode::Ready);
  assert(left.render(panel.data() + 1, &right));
  assert(panel == full_photo_pair);
  assert(right.decode(companion.data(), companion.size(), {PreviewTarget::PairPane, DisplayMode::Fit, DisplayMode::Fill}) == Decode::Ready);
  std::fill(panel.begin() + 1, panel.end() - 1, guard);
  const PreparationCheckpoint interrupt{nullptr, [](void *) { return false; }};
  // An interrupted Fill leaves the panel untouched before either pane is painted.
  assert(!left.render(panel.data() + 1, &right, DisplayMode::Fill, ScreenRotation::Normal, interrupt));
  assert(std::all_of(panel.begin(), panel.end(), [](uint16_t pixel) { return pixel == guard; }));
  assert(left.render(panel.data() + 1, &right, DisplayMode::Fill));
  assert(std::count(panel.begin() + 1, panel.end() - 1, 0) == 0);
  assert((at(panel, 0, 420) & 0xf800) >= 0xf000 && (at(panel, 1279, 420) & 0x001f) >= 0x001e);
  assert(at(panel, 0, 0) != 0 && at(panel, 1279, 799) != 0);
  assert((at(panel, 639, 420) & 0xf800) >= 0xf000 && (at(panel, 640, 420) & 0x001f) >= 0x001e);
  expect_flipped(panel, left, &right, DisplayMode::Fill);
  assert(replay_pair_fills_panes(panel.data() + 1, ScreenRotation::Normal));
  std::vector<uint16_t> rotated(pixel_count);
  assert(left.render(rotated.data(), &right, DisplayMode::Fill, ScreenRotation::UpsideDown));
  assert(replay_pair_fills_panes(rotated.data(), ScreenRotation::UpsideDown));
  const auto seam = panel_offset(portrait_pair_width, panel_width - 1, ScreenRotation::UpsideDown);
  const auto seam_pixel = rotated[seam];
  rotated[seam] = 0x1082;
  assert(!replay_pair_fills_panes(rotated.data(), ScreenRotation::UpsideDown));
  rotated[seam] = seam_pixel;
  rotated[panel_offset(slide_width - 1, 0, ScreenRotation::UpsideDown)] = 0;
  assert(!replay_pair_fills_panes(rotated.data(), ScreenRotation::UpsideDown));
  assert(right.decode(companion.data(), companion.size() / 2, {PreviewTarget::PairPane}) == Decode::Invalid);
  left.render(panel.data() + 1, &right);
  assert((at(panel, 640, 420) & 0xf800) >= 0xf000 && at(panel, 0, 0) != 0 && at(panel, 1279, 799) != 0);
  fail_heap_allocations = true;
  assert(right.decode(companion.data(), companion.size(), {PreviewTarget::PairPane}) == Decode::NoMemory);
  fail_heap_allocations = false;
  assert(left.decode(primary.data(), primary.size(), {PreviewTarget::OptionalPair}) == Decode::Ready);
  left.render(panel.data() + 1, &right, DisplayMode::Fill);
  assert((at(panel, 640, 420) & 0xf800) >= 0xf000 && at(panel, 0, 420) == 0);

  assert(right.decode(companion.data(), companion.size(), {PreviewTarget::Slide, DisplayMode::Fill}) == Decode::Ready);
  assert(right.render(panel.data() + 1));
  assert((at(panel, 0, 420) & 0x001f) >= 0x001e && at(panel, 1279, 799) != 0);
  expect_flipped(panel, right, nullptr);

  // In a required pair, both sides decode directly to their final pane.
  assert(left.decode(primary.data(), primary.size(), {PreviewTarget::PairPane, DisplayMode::Fit, DisplayMode::Fill}) == Decode::Ready);
  assert(right.decode(companion.data(), companion.size(), {PreviewTarget::PairPane, DisplayMode::Fit, DisplayMode::Fill}) == Decode::Ready);
  left.render(panel.data() + 1, &right, DisplayMode::Fill);
  assert((at(panel, 0, 420) & 0xf800) >= 0xf000 && (at(panel, 1279, 420) & 0x001f) >= 0x001e);
  assert((at(panel, 639, 420) & 0xf800) >= 0xf000 && (at(panel, 640, 420) & 0x001f) >= 0x001e);
  expect_flipped(panel, left, &right, DisplayMode::Fill);
  const auto pair_panes = panel;
  assert(left.decode(primary.data(), primary.size(),
                     {PreviewTarget::RequiredPair, DisplayMode::Fit, DisplayMode::Fill}) == Decode::Ready);
  assert(left.portrait() && left.render(panel.data() + 1, &right, DisplayMode::Fill) && panel == pair_panes);

  // A reframe can lose an optional companion after decoding the primary
  // into its pane. The standalone fallback crops from the original preview.
  assert(left.decode(primary.data(), primary.size(), {PreviewTarget::PairPane, DisplayMode::Fill, DisplayMode::Fill}) ==
         Decode::Ready);
  assert(left.decode(primary.data(), primary.size(), {PreviewTarget::Slide, DisplayMode::Fill}) == Decode::Ready);
  assert(left.render(panel.data() + 1));
  assert(std::count(panel.begin() + 1, panel.end() - 1, 0) == 0);
  assert(panel.front() == guard && panel.back() == guard);
  expect_flipped(panel, left);
}

void verify_landscape_with_required_pairs(const char *path) {
  const auto photo = load(path);
  DecodedPreview decoded;
  std::vector<uint16_t> single(pixel_count);
  std::vector<uint16_t> pairs_only(pixel_count);
  assert(decoded.decode(photo.data(), photo.size(), {PreviewTarget::Slide, DisplayMode::Fill}) == Decode::Ready);
  assert(!decoded.portrait() && decoded.render(single.data()));
  assert(decoded.decode(photo.data(), photo.size(),
                        {PreviewTarget::RequiredPair, DisplayMode::Fill, DisplayMode::Fit}) == Decode::Ready);
  assert(!decoded.portrait() && decoded.render(pairs_only.data()));
  assert(single == pairs_only && pairs_only.front() != 0 && pairs_only.back() != 0);
  for (const auto mode : {DisplayMode::Fit, DisplayMode::Fill}) {
    assert(decoded.decode(photo.data(), photo.size(), {PreviewTarget::RequiredPair, mode, DisplayMode::Fit}) ==
           Decode::Ready);
    assert(decoded.render(pairs_only.data()));
    assert(decoded.decode(photo.data(), photo.size(), {PreviewTarget::Slide, mode, DisplayMode::Fit}, nullptr, {},
                          PhotoOrientation::Landscape) == Decode::Ready);
    assert(decoded.render(single.data()) && single == pairs_only);
  }
}

void verify_odd_jpeg_rows(const char *path) {
  const auto photo = load(path);
  constexpr uint16_t guard = 0x43cd;
  std::vector<uint16_t> panel(pixel_count + 2, guard);
  DecodedPreview decoded;
  // Fit keeps the odd-width source at its original size. Both an aligned row
  // and an unaligned row must land at the start of the intended scanline.
  assert(decoded.decode(photo.data(), photo.size()) == Decode::Ready);
  assert(decoded.render(panel.data() + 1));
  for (const int y : {160, 161, 638, 639}) {
    const bool top = y < 400;
    assert(at(panel, 0, y) == (top ? 0xf800 : 0x001f));
    assert(at(panel, 1278, y) == (top ? 0x07e0 : 0xffff));
    assert(at(panel, 1279, y) == 0);
  }
  expect_flipped(panel, decoded);
  assert(decoded.decode(photo.data(), photo.size(), {PreviewTarget::Slide, DisplayMode::Fill}) == Decode::Ready);
  assert(decoded.render(panel.data() + 1, nullptr, DisplayMode::Fill));
  assert(at(panel, 1279, 160) == 0x07e0 && at(panel, 1279, 639) == 0xffff);
  assert(panel.front() == guard && panel.back() == guard);
  assert(decoded.decode(photo.data(), photo.size() / 2) == Decode::Invalid);
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
  assert(decoded.decode(progressive.data(), progressive.size(), {}, &hardware) == Decode::Ready);
  assert(!decoded.hardware_jpeg() && hardware_calls == 0);
  assert(decoded.decode(webp.data(), webp.size(), {}, &hardware) == Decode::Ready);
  assert(!decoded.hardware_jpeg() && hardware_calls == 0);
  assert(decoded.decode(baseline.data(), baseline.size() / 2, {}, &hardware) == Decode::Invalid);
  assert(!decoded.hardware_jpeg() && hardware_calls == 0);

  assert(decoded.decode(baseline.data(), baseline.size(), {}, &hardware) == Decode::Ready);
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
  assert(decoded.decode(baseline.data(), baseline.size(), {PreviewTarget::Slide, DisplayMode::Fill}, &hardware) == Decode::Ready);
  assert(decoded.hardware_jpeg() && hardware_calls == 2 && engine_creates == 1);
  decoded.render(panel.data() + 1, nullptr, DisplayMode::Fill);
  assert(at(panel, 0, 0) == 0xf800 && at(panel, slide_width - 1, slide_height - 1) == 0xffff);

  dma_allocations_fail = true;
  assert(decoded.decode(baseline.data(), baseline.size(), {}, &hardware) == Decode::Ready);
  assert(!decoded.hardware_jpeg() && hardware_calls == 2);
  dma_allocations_fail = false;
  short_output = true;
  assert(decoded.decode(baseline.data(), baseline.size(), {}, &hardware) == Decode::Ready);
  assert(!decoded.hardware_jpeg() && hardware_calls == 3 && engine_creates == 1);
  short_output = false;
  force_timeout = true;
  assert(decoded.decode(baseline.data(), baseline.size(), {}, &hardware) == Decode::Ready);
  assert(!decoded.hardware_jpeg() && hardware_calls == 4);
  force_timeout = false;
  assert(decoded.decode(baseline.data(), baseline.size(), {}, &hardware) == Decode::Ready);
  assert(!decoded.hardware_jpeg() && hardware_calls == 4 && engine_creates == 1);
}

void verify_scaled_jpeg(const char *path) {
  const auto photo = load(path);
  DecodedPreview decoded;
  constexpr PreviewLayout pane{PreviewTarget::PairPane, DisplayMode::Fit, DisplayMode::Fill};
  assert(decoded.decode(photo.data(), photo.size(), pane) == Decode::Ready && decoded.portrait());
  constexpr uint16_t guard = 0x43cd;
  std::vector<uint16_t> panel(pixel_count + 2, guard);
  assert(decoded.render(panel.data() + 1, &decoded, DisplayMode::Fill));
  assert(panel.front() == guard && panel.back() == guard);
  // The 961x1441 JPEG takes the fractional IDCT path for a 638x800 pane.
  // Center cropping removes the cyan bands at the top and bottom.
  assert(at(panel, 0, 0) == 0xf800 && at(panel, 635, 0) == 0x07e0);
  assert(at(panel, 0, 799) == 0x001f && at(panel, 635, 799) == 0xffff);
  assert(replay_pair_fills_panes(panel.data() + 1, ScreenRotation::Normal));
  expect_flipped(panel, decoded, &decoded, DisplayMode::Fill);
  assert(decoded.decode(photo.data(), photo.size() / 2, pane) == Decode::Invalid);
  assert(decoded.decode(photo.data(), photo.size() - 8, pane) == Decode::Invalid);
}

void verify_interruption(const char *jpeg_path, const char *progressive_path, const char *webp_path) {
  const auto jpeg = load(jpeg_path);
  const auto progressive = load(progressive_path);
  const auto webp = load(webp_path);
  int remaining = 2;
  const PreparationCheckpoint checkpoint{&remaining, [](void *context) {
    return --*static_cast<int *>(context) >= 0;
  }};
  DecodedPreview decoded;
  assert(decoded.decode(webp.data(), webp.size(), {}, nullptr, checkpoint) == Decode::Interrupted);
  RecentPreviews downloads;
  AssetId webp_id{};
  webp_id.front() = 'w';
  downloads.remember_interrupted(webp_id, webp);
  const auto saved_webp = downloads.find(webp_id);
  assert(!saved_webp.empty() && decoded.decode(saved_webp.data(), saved_webp.size()) == Decode::Ready);
  remaining = 3;
  assert(decoded.decode(jpeg.data(), jpeg.size(), {}, nullptr, checkpoint) == Decode::Interrupted);

  // The malformed final scan can only be detected after the earlier scans
  // have been read. A swipe should be able to interrupt the read before then.
  auto malformed = progressive;
  constexpr std::array<uint8_t, 2> scan_marker{0xff, 0xda};
  const auto last_scan = std::find_end(malformed.begin(), malformed.end(), scan_marker.begin(), scan_marker.end());
  assert(malformed.end() - last_scan >= 4 && last_scan[2] == 0);
  last_scan[3] = 1;
  assert(decoded.decode(malformed.data(), malformed.size()) == Decode::Invalid);
  remaining = 4;
  assert(decoded.decode(malformed.data(), malformed.size(), {}, nullptr, checkpoint) ==
         Decode::Interrupted);
  remaining = 4;
  assert(decoded.decode(progressive.data(), progressive.size(), {}, nullptr, checkpoint) ==
         Decode::Interrupted);
  AssetId progressive_id{};
  progressive_id.front() = 'j';
  downloads.remember_interrupted(progressive_id, progressive);
  const auto saved_progressive = downloads.find(progressive_id);
  assert(!saved_progressive.empty() &&
         decoded.decode(saved_progressive.data(), saved_progressive.size()) == Decode::Ready);

  P4JpegDecoder hardware;
  const auto before = hardware_calls;
  remaining = 4;
  assert(decoded.decode(jpeg.data(), jpeg.size(), {}, &hardware, checkpoint) == Decode::Interrupted);
  assert(hardware_calls == before + 1 && !decoded.hardware_jpeg());

  assert(decoded.decode(jpeg.data(), jpeg.size()) == Decode::Ready);
  std::vector<uint16_t> panel(pixel_count, 0xffff);
  remaining = 1;
  assert(!decoded.render(panel.data(), nullptr, DisplayMode::Fit, ScreenRotation::Normal, checkpoint));
  assert(panel.front() == 0 && panel.back() == 0xffff);
  assert(decoded.render(panel.data()));

  assert(decoded.decode(jpeg.data(), jpeg.size(), {PreviewTarget::Slide, DisplayMode::Fill}) == Decode::Ready);
  constexpr uint16_t untouched = 0x43cd;
  std::vector<uint16_t> complete(pixel_count);
  for (const auto rotation : {ScreenRotation::Normal, ScreenRotation::UpsideDown}) {
    assert(decoded.render(complete.data(), nullptr, DisplayMode::Fill, rotation));
    std::fill(panel.begin(), panel.end(), untouched);
    remaining = 1;
    assert(!decoded.render(panel.data(), nullptr, DisplayMode::Fill, rotation, checkpoint));
    for (int x = 0; x < slide_width; ++x) {
      for (int y = 0; y < slide_height; ++y) {
        const auto pixel = panel_offset(x, panel_width - 1 - y, rotation);
        assert(panel[pixel] == (y < 32 && x < 256 ? complete[pixel] : untouched));
      }
    }
  }
}

void verify_lossy_webp_interruption(const char *path) {
  const auto photo = load(path);
  int checkpoints = 0;
  const PreparationCheckpoint interrupt{&checkpoints, [](void *context) {
    return ++*static_cast<int *>(context) < 2;
  }};
  DecodedPreview decoded;
  assert(decoded.decode(photo.data(), photo.size(), {PreviewTarget::Slide, DisplayMode::Fill}, nullptr, interrupt) == Decode::Interrupted);
  assert(checkpoints == 2);
  assert(decoded.decode(photo.data(), photo.size() - 2, {PreviewTarget::Slide, DisplayMode::Fill}) == Decode::Invalid);
  assert(decoded.decode(photo.data(), photo.size(), {PreviewTarget::Slide, DisplayMode::Fill}) == Decode::Ready);
}

void verify_orientation(const char *square_jpeg, const char *square_webp, const char *portrait) {
  DecodedPreview decoded;
  const auto portrait_bytes = load(portrait);
  for (const auto *path : {square_jpeg, square_webp}) {
    const auto bytes = load(path);
    fail_heap_allocations = true;
    assert(decoded.decode(bytes.data(), bytes.size(), {}, nullptr, {}, PhotoOrientation::Portrait) ==
           Decode::OrientationMismatch);
    fail_heap_allocations = false;
    assert(decoded.decode(bytes.data(), bytes.size(), {PreviewTarget::Slide, DisplayMode::Fill}, nullptr, {}, PhotoOrientation::Landscape) ==
           Decode::Ready);
    assert(!decoded.portrait());
  }
  assert(decoded.decode(portrait_bytes.data(), portrait_bytes.size(), {}, nullptr, {},
                        PhotoOrientation::Landscape) == Decode::OrientationMismatch);
  assert(decoded.decode(portrait_bytes.data(), portrait_bytes.size(), {}, nullptr, {},
                        PhotoOrientation::Portrait) == Decode::Ready && decoded.portrait());
}

void verify_rotation_when_the_encoded_preview_is_gone() {
  constexpr auto normal = ScreenRotation::Normal;
  constexpr auto inverted = ScreenRotation::UpsideDown;
  constexpr uint16_t guard = 0xaaaa;
  const Viewport old{normal};
  std::vector<uint16_t> source(pixel_count);
  std::vector<uint16_t> destination(pixel_count + 2, guard);
  constexpr uint16_t colors[2][2]{{0xf800, 0x07e0}, {0x001f, 0xffff}};
  for (int y = 0; y < old.height; ++y) {
    for (int x = 0; x < old.width; ++x)
      source[old.offset(x, y)] = colors[y >= old.height / 2][x >= old.width / 2];
  }
  auto *output = destination.data() + 1;
  assert(reframe_cached_photo(output, source.data(), normal, inverted, false, DisplayMode::Fit, {}));
  assert(std::equal(source.rbegin(), source.rend(), output));
  for (auto rotation : {ScreenRotation::Clockwise, ScreenRotation::Counterclockwise}) {
    const Viewport next{rotation};
    const auto pixel = [&](int x, int y) { return output[next.offset(x, y)]; };
    assert(reframe_cached_photo(output, source.data(), normal, rotation, false, DisplayMode::Fit, {}));
    assert(pixel(100, 0) == 0 && pixel(100, next.height - 1) == 0);
    assert(pixel(100, 450) == colors[0][0] && pixel(700, 450) == colors[0][1]);
    assert(pixel(100, 850) == colors[1][0] && pixel(700, 850) == colors[1][1]);
    assert(reframe_cached_photo(output, source.data(), normal, rotation, true, DisplayMode::Fill, {}));
    assert(pixel(100, 0) == colors[0][0] && pixel(700, 0) == colors[0][0]);
    assert(pixel(100, next.height - 1) == colors[1][0] && pixel(700, next.height - 1) == colors[1][0]);
    assert(destination.front() == guard && destination.back() == guard);
  }
  const PreparationCheckpoint interrupted{nullptr, [](void *) { return false; }};
  assert(!reframe_cached_photo(output, source.data(), inverted, normal, false, DisplayMode::Fill, interrupted));
}

int main(int argc, char **argv) {
  assert(argc == 10);
  const auto wide = frame_photo(1600, 900, slide_width, slide_height, DisplayMode::Fill);
  assert(wide.source.x == 80 && wide.source.y == 0 && wide.source.width == 1440 && wide.source.height == 900);
  const auto narrow = frame_photo(600, 500, slide_width, slide_height, DisplayMode::Fill);
  assert(narrow.source.x == 0 && narrow.source.y == 62 && narrow.source.width == 600 && narrow.source.height == 375);
  for (int i = 1; i < 4; ++i)
    verify(argv[i]);
  verify_pair(argv[4], argv[5]);
  verify_landscape_with_required_pairs(argv[1]);
  verify_landscape_with_required_pairs(argv[7]);
  verify_odd_jpeg_rows(argv[6]);
  verify_hardware(argv[1], argv[2], argv[3]);
  verify_interruption(argv[1], argv[2], argv[3]);
  verify_lossy_webp_interruption(argv[7]);
  verify_scaled_jpeg(argv[8]);
  verify_scaled_jpeg(argv[9]);
  verify_orientation(argv[1], argv[3], argv[4]);
  verify_rotation_when_the_encoded_preview_is_gone();
  const uint8_t unsupported[]{0, 1, 2, 3};
  DecodedPreview decoded;
  assert(decoded.decode(unsupported, sizeof(unsupported)) == Decode::Invalid);
  std::vector<uint16_t> panel(pixel_count + 2, 0xffff);
  assert(decoded.render(panel.data() + 1, nullptr, DisplayMode::Fit, ScreenRotation::UpsideDown));
  assert(std::count(panel.begin(), panel.end(), 0) == pixel_count);
  assert(panel.front() == 0xffff && panel.back() == 0xffff);
}
