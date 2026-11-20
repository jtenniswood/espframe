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

enum class ScreenRotation : uint8_t { Normal, UpsideDown };

inline constexpr int axis_direction(ScreenRotation rotation) {
  return rotation == ScreenRotation::UpsideDown ? -1 : 1;
}

inline constexpr int oriented_axis(int coordinate, int extent, ScreenRotation rotation) {
  return rotation == ScreenRotation::UpsideDown ? extent - 1 - coordinate : coordinate;
}

inline constexpr int panel_offset(int row, int column, ScreenRotation rotation) {
  return oriented_axis(row * panel_width + column, pixel_count, rotation);
}

}  // namespace espframe::native
