#pragma once

#include <algorithm>
#include <cstdint>

namespace espframe::native {

using SlideIndex = int64_t;
using PixelPosition = int64_t;

struct SlideRange {
  SlideIndex first;
  SlideIndex last;

  bool contains(SlideIndex slide) const { return slide >= first && slide <= last; }
  void include(SlideIndex slide) {
    first = std::min(first, slide);
    last = std::max(last, slide);
  }
};

inline constexpr int panel_width = 800;
inline constexpr int panel_height = 1280;
inline constexpr int slide_width = panel_height;
inline constexpr int slide_height = panel_width;
inline constexpr int pixel_count = panel_width * panel_height;
inline constexpr int portrait_pair_width = slide_width / 2;

enum class ScreenRotation : uint8_t { Normal, UpsideDown, Clockwise, Counterclockwise };

inline constexpr bool portrait_screen(ScreenRotation rotation) {
  return rotation == ScreenRotation::Clockwise || rotation == ScreenRotation::Counterclockwise;
}

inline constexpr int axis_direction(ScreenRotation rotation) {
  return rotation == ScreenRotation::UpsideDown || rotation == ScreenRotation::Clockwise ? -1 : 1;
}

inline constexpr int oriented_axis(int coordinate, int extent, ScreenRotation rotation) {
  return axis_direction(rotation) < 0 ? extent - 1 - coordinate : coordinate;
}

inline constexpr int panel_offset(int row, int column, ScreenRotation rotation) {
  return oriented_axis(row * panel_width + column, pixel_count, rotation);
}

// The physical panel scans 800-pixel rows. Logical landscape x travels
// along those rows, while logical portrait x travels across each row.
struct Viewport {
  explicit constexpr Viewport(ScreenRotation rotation)
      : portrait(portrait_screen(rotation)), width(portrait ? panel_width : slide_width),
        height(portrait ? panel_height : slide_height),
        x_stride(axis_direction(rotation) * (portrait ? 1 : panel_width)),
        y_stride(axis_direction(rotation) * (portrait ? panel_width : -1)),
        origin((x_stride < 0 ? -(width - 1) * x_stride : 0) +
               (y_stride < 0 ? -(height - 1) * y_stride : 0)) {}

  constexpr int offset(int x, int y) const { return origin + x * x_stride + y * y_stride; }

  bool portrait;
  int width;
  int height;
  int x_stride;
  int y_stride;
  int origin;
};

}  // namespace espframe::native
