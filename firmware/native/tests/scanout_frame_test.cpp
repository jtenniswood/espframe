#include "scanout_frame.h"
#include "slide_cache.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

using namespace espframe::native;

namespace {

void verify_source_lifetime() {
  uint16_t memory[3]{};
  SlideCache::Buffers buffers{&memory[0], &memory[1], &memory[2]};
  SlideCache cache(buffers);
  for (SlideIndex slide = 3; slide <= 5; ++slide) {
    const auto lease = cache.reserve(slide, 3, 5);
    assert(lease && cache.complete(*lease, true));
  }
  ScanoutSources sources;
  assert(!sources.active());
  sources.submit(SlideRange{3, 4});
  assert(sources.active() && sources.retain({5, 5}).first == 3);
  sources.latch();

  const auto *old = cache.find(3);
  sources.submit(SlideRange{4, 5});
  const auto protected_range = sources.retain({5, 5});
  assert(protected_range.first == 3 && protected_range.last == 5);
  assert(!cache.reserve(6, protected_range.first, protected_range.last));

  sources.latch();
  const auto remaining = sources.retain({5, 5});
  assert(remaining.first == 4 && remaining.last == 5);
  const auto reuse = cache.reserve(6, remaining.first, remaining.last);
  assert(reuse && reuse->pixels == old && cache.find(3) == nullptr);

  sources.submit({});
  assert(sources.active() && sources.retain({5, 5}).first == 4);
  sources.latch();
  assert(!sources.active() && sources.retain({5, 5}).first == 5);
}

size_t verify_frame(const ScanoutFrame &frame, const std::vector<uint16_t> &reference,
                    const std::vector<uint16_t> &owned) {
  constexpr size_t frame_bytes = pixel_count * sizeof(uint16_t);
  const auto owned_start = reinterpret_cast<uintptr_t>(owned.data());
  size_t checked = 0;
  size_t painted = 0;
  assert(frame.count == ScanoutFrame::segment_count);
  for (unsigned i = 0; i < frame.count; ++i) {
    const auto bytes = frame.lengths[i];
    const auto *pixels = frame.sources[i];
    assert(bytes > 0 && bytes % ScanoutFrame::row_bytes == 0 && checked + bytes <= frame_bytes);
    assert(std::memcmp(reference.data() + checked / sizeof(*pixels), pixels, bytes) == 0);
    const auto start = reinterpret_cast<uintptr_t>(pixels);
    if (start >= owned_start && start - owned_start < frame_bytes)
      painted += bytes;
    checked += bytes;
  }
  assert(checked == frame_bytes);
  return painted;
}

void prepare_clock(ClockOverlay &clock, const std::vector<uint8_t> &atlas, ScreenRotation rotation,
                   bool show, const char *format = "24 Hour") {
  ClockSettings settings;
  settings.show = show;
  settings.format = format;
  assert(clock.init(settings, atlas.data(), false, rotation));
  assert(!clock.visible());
  std::tm time{};
  time.tm_hour = 17;
  time.tm_min = 48;
  clock.update(time);
  assert(clock.visible() == show);
}

void verify_cached(const std::vector<uint8_t> &atlas, const std::vector<uint16_t> &left,
                   const std::vector<uint16_t> &right, ScreenRotation rotation) {
  std::vector<uint16_t> reference(pixel_count), owned(pixel_count);
  const auto copy = [](auto *to, const auto *from, int rows) { copy_slide_rows(to, from, panel_width, rows); };
  for (const bool show_clock : {false, true}) {
    for (const auto format : {"12 Hour", "24 Hour"}) {
      ClockOverlay clock;
      prepare_clock(clock, atlas, rotation, show_clock, format);
      for (int offset = 0; offset < panel_height; ++offset) {
        const auto photos = orient_slide_span(left.data(), offset == 0 ? nullptr : right.data(), panel_height, offset, rotation);
        blit_slides(reference.data(), photos.first, photos.second, panel_width, panel_height, photos.offset);
        clock.draw(reference.data());
        std::fill(owned.begin(), owned.end(), 0xf81f);
        const auto frame = prepare_scanout(owned.data(), photos, clock.visible(), rotation, copy);
        clock.draw(owned.data());
        assert(!frame.resisted);
        assert(verify_frame(frame, reference, owned) == (show_clock ? clock_band_rows * ScanoutFrame::row_bytes : 0));
      }
    }
  }
}

void verify_edge(const std::vector<uint8_t> &atlas, const std::vector<uint16_t> &photo, ScreenRotation rotation) {
  std::vector<uint16_t> reference(pixel_count), owned(pixel_count);
  const auto copy = [](auto *to, const auto *from, int rows) { copy_slide_rows(to, from, panel_width, rows); };
  for (const bool show_clock : {false, true}) {
    ClockOverlay clock;
    prepare_clock(clock, atlas, rotation, show_clock);
    // Cover the maximum physical resistance in either direction, including
    // the cases where the strip and clock need separate owned bands.
    for (int pull = -48; pull <= 48; ++pull) {
      blit_slide_edge(reference.data(), photo.data(), panel_width, panel_height, pull);
      clock.draw(reference.data());
      std::fill(owned.begin(), owned.end(), 0xf81f);
      const auto frame = prepare_edge_scanout(owned.data(), photo.data(), pull, clock.visible(), rotation, copy);
      assert(frame && frame->resisted);
      clock.draw(owned.data());
      const int clock_rows = show_clock ? clock_band_rows : 0;
      const int edge_rows = std::abs(pull);
      const bool same_side = (rotation == ScreenRotation::Normal) == (pull <= 0);
      const int painted_rows = same_side ? std::max(clock_rows, edge_rows) : clock_rows + edge_rows;
      assert(verify_frame(*frame, reference, owned) == painted_rows * ScanoutFrame::row_bytes);
    }
  }
  const int pull = axis_direction(rotation) * (panel_height - clock_band_rows + 1);
  assert(!prepare_edge_scanout(owned.data(), photo.data(), pull, true, rotation, copy));
}

void verify_caption_band(const std::vector<uint16_t> &left, const std::vector<uint16_t> &right,
                         ScreenRotation rotation) {
  std::vector<uint16_t> reference(pixel_count), owned(pixel_count);
  const auto copy = [](auto *to, const auto *from, int rows) { copy_slide_rows(to, from, panel_width, rows); };
  constexpr int caption_rows = 260;
  const int first = rotation == ScreenRotation::Normal ? panel_height - caption_rows : 0;
  const auto paint_caption = [&](auto &frame) {
    for (int row = first; row < first + caption_rows; ++row)
      frame[row * panel_width + 80] = 0xffff;
  };
  for (const bool clock : {false, true}) {
    for (const int offset : {0, 42, 200, 550, 800, 1040, 1238}) {
      const auto span = orient_slide_span(left.data(), offset == 0 ? nullptr : right.data(), panel_height, offset, rotation);
      blit_slides(reference.data(), span.first, span.second, panel_width, panel_height, span.offset);
      paint_caption(reference);
      std::fill(owned.begin(), owned.end(), 0xf81f);
      const auto frame = prepare_scanout(owned.data(), span, clock, rotation, copy, caption_rows);
      paint_caption(owned);
      const auto painted = verify_frame(frame, reference, owned);
      const auto overlay_bytes = (caption_rows + (clock ? clock_band_rows : 0)) * ScanoutFrame::row_bytes;
      assert(painted >= overlay_bytes && painted < pixel_count * sizeof(uint16_t));
      if (!clock)
        assert(painted == overlay_bytes);
    }
    for (const int pull : {-42, 42}) {
      blit_slide_edge(reference.data(), left.data(), panel_width, panel_height, pull);
      const auto frame = prepare_edge_scanout(owned.data(), left.data(), pull, clock, rotation, copy, caption_rows);
      assert(frame && frame->resisted);
      paint_caption(reference);
      paint_caption(owned);
      verify_frame(*frame, reference, owned);
    }
  }
}

}  // namespace

int main(int argc, char **argv) {
  assert(argc == 2);
  std::ifstream file(argv[1], std::ios::binary);
  const std::vector<uint8_t> atlas{std::istreambuf_iterator<char>(file), {}};
  assert(atlas.size() == ClockOverlay::atlas_size);
  std::vector<uint16_t> left(pixel_count), right(pixel_count);
  uint32_t random = 19;
  for (int i = 0; i < pixel_count; ++i) {
    random = random * 1664525U + 1013904223U;
    left[i] = random >> 16;
    random = random * 1664525U + 1013904223U;
    right[i] = random >> 16;
  }
  verify_source_lifetime();
  for (const auto rotation : {ScreenRotation::Normal, ScreenRotation::UpsideDown}) {
    verify_cached(atlas, left, right, rotation);
    verify_edge(atlas, left, rotation);
    verify_caption_band(left, right, rotation);
  }
  std::cout << "Linked scanout matches full frames and retains cache sources through handoff\n";
}
