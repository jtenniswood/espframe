#pragma once

#include "clock_overlay.h"
#include "slide_blit.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <optional>

namespace espframe::native {

// Both the frame already being scanned and a queued replacement can still read
// their photos. A cache slot becomes reclaimable only after the new frame latches.
class ScanoutSources {
 public:
  void submit(std::optional<SlideRange> slides) { pending_ = slides; }
  void latch() {
    front_ = pending_;
    pending_.reset();
  }
  bool active() const { return front_.has_value() || pending_.has_value(); }
  SlideRange retain(SlideRange range) const {
    for (const auto &slides : {front_, pending_}) {
      if (slides) {
        range.include(slides->first);
        range.include(slides->last);
      }
    }
    return range;
  }

 private:
  std::optional<SlideRange> front_;
  std::optional<SlideRange> pending_;
};

struct ScanoutFrame {
  static constexpr unsigned segment_count = 3;
  static constexpr size_t row_bytes = panel_width * sizeof(uint16_t);
  std::array<const uint16_t *, segment_count> sources{};
  std::array<size_t, segment_count> lengths{};
  unsigned count{};
  uint32_t prepare_us{};
  bool resisted{};

  void add(const uint16_t *pixels, int rows) {
    if (rows == 0)
      return;
    assert(count < segment_count && rows > 0);
    sources[count] = pixels;
    lengths[count++] = rows * row_bytes;
  }

  void complete() {
    while (count < segment_count) {
      const auto biggest = std::max_element(lengths.begin(), lengths.begin() + count) - lengths.begin();
      const size_t first_bytes = lengths[biggest] / row_bytes / 2 * row_bytes;
      assert(first_bytes > 0);
      std::move_backward(sources.begin() + biggest + 1, sources.begin() + count, sources.begin() + count + 1);
      std::move_backward(lengths.begin() + biggest + 1, lengths.begin() + count, lengths.begin() + count + 1);
      sources[biggest + 1] = sources[biggest] + first_bytes / sizeof(uint16_t);
      lengths[biggest + 1] = lengths[biggest] - first_bytes;
      lengths[biggest] = first_bytes;
      ++count;
    }
  }
};

inline constexpr int clock_band_rows = ClockOverlay::first_row + ClockOverlay::rows;

struct ScanoutBands {
  int prefix;
  int suffix;
};

inline ScanoutBands overlay_bands(bool show_clock, int caption_rows, ScreenRotation rotation) {
  const int clock = show_clock ? clock_band_rows : 0;
  return rotation == ScreenRotation::Normal ? ScanoutBands{clock, caption_rows} : ScanoutBands{caption_rows, clock};
}

template<class CopyRows>
ScanoutFrame prepare_scanout(uint16_t *output, SlideSpan photos, bool show_clock, ScreenRotation rotation,
                             CopyRows copy_rows, int caption_rows = 0) {
  auto [prefix, suffix] = overlay_bands(show_clock, caption_rows, rotation);
  const int first_end = panel_height - photos.offset;
  int suffix_first = std::max(prefix, panel_height - suffix);
  // Two decorations on opposite sides can split the photos into four
  // slices. Extend the nearer painted band to the seam only in that case.
  if (prefix && suffix && first_end > prefix && first_end < suffix_first) {
    if (first_end - prefix <= suffix_first - first_end)
      prefix = first_end;
    else
      suffix_first = first_end;
  }
  const auto photo_at = [&](int row) {
    if (row < first_end)
      return photos.first + (row + photos.offset) * panel_width;
    return photos.second + (row - first_end) * panel_width;
  };
  ScanoutFrame frame;
  for (int row = 0; row < panel_height;) {
    const bool painted = row < prefix || row >= suffix_first;
    const int photo_end = row < first_end ? first_end : panel_height;
    const int end = row < prefix ? prefix : row >= suffix_first ? panel_height : std::min(photo_end, suffix_first);
    if (painted) {
      for (int painted_row = row; painted_row < end;) {
        const int copy_end = painted_row < first_end ? std::min(first_end, end) : end;
        copy_rows(output + painted_row * panel_width, photo_at(painted_row), copy_end - painted_row);
        painted_row = copy_end;
      }
    }
    frame.add(painted ? output + row * panel_width : photo_at(row), end - row);
    row = end;
  }
  frame.complete();
  return frame;
}

template<class CopyRows>
std::optional<ScanoutFrame> prepare_edge_scanout(uint16_t *output, const uint16_t *photo, int pull,
                                                bool show_clock, ScreenRotation rotation,
                                                CopyRows copy_rows, int caption_rows = 0) {
  const int strip = std::abs(pull);
  const int photo_first = pull < 0 ? strip : 0;
  const int photo_end = pull < 0 ? panel_height : panel_height - strip;
  const auto [prefix, suffix] = overlay_bands(show_clock, caption_rows, rotation);
  const int borrowed_first = std::max(photo_first, prefix);
  const int borrowed_end = std::min(photo_end, panel_height - suffix);
  if (borrowed_first >= borrowed_end)
    return {};

  const auto photo_at = [&](int row) {
    return photo + (row - photo_first + (pull > 0 ? strip : 0)) * panel_width;
  };
  const auto paint = [&](int first, int end) {
    const int copy_first = std::max(first, photo_first);
    const int copy_end = std::min(end, photo_end);
    if (copy_first < copy_end)
      copy_rows(output + copy_first * panel_width, photo_at(copy_first), copy_end - copy_first);
    for (int row = first; row < std::min(end, photo_first); ++row)
      copy_slide_rows(output + row * panel_width, photo, panel_width, 1);
    for (int row = std::max(first, photo_end); row < end; ++row)
      copy_slide_rows(output + row * panel_width, photo + (panel_height - 1) * panel_width, panel_width, 1);
  };
  paint(0, borrowed_first);
  paint(borrowed_end, panel_height);

  ScanoutFrame frame;
  frame.resisted = true;
  frame.add(output, borrowed_first);
  frame.add(photo_at(borrowed_first), borrowed_end - borrowed_first);
  frame.add(output + borrowed_end * panel_width, panel_height - borrowed_end);
  frame.complete();
  return frame;
}

}  // namespace espframe::native
