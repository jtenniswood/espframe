#pragma once

#include "slide_geometry.h"

namespace espframe::native {

// The synthetic portraits have nonblack content right through the crop.
// Sample both panes and the divider before tint or captions are applied.
inline bool replay_pair_fills_panes(const uint16_t *panel, ScreenRotation rotation) {
  constexpr int rows[]{0, slide_height / 2, slide_height - 1};
  constexpr int right = portrait_pair_width + portrait_pair_gap;
  for (const auto row : rows) {
    const auto column = panel_width - 1 - row;
    if (!panel[panel_offset(0, column, rotation)] || !panel[panel_offset(portrait_pair_width - 1, column, rotation)] ||
        !panel[panel_offset(right, column, rotation)] || !panel[panel_offset(slide_width - 1, column, rotation)])
      return false;
    for (int x = portrait_pair_width; x < right; ++x) {
      if (panel[panel_offset(x, column, rotation)])
        return false;
    }
  }
  return true;
}

}  // namespace espframe::native
