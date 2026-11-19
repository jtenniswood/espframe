#pragma once

#include <cstdint>

namespace espframe::native {

using SlideIndex = int64_t;
using PixelPosition = int64_t;

inline constexpr int panel_width = 800;
inline constexpr int panel_height = 1280;
inline constexpr int slide_width = panel_height;
inline constexpr int slide_height = panel_width;
inline constexpr int pixel_count = panel_width * panel_height;

}  // namespace espframe::native
