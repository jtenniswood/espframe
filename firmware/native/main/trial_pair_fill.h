#pragma once

#include "slide_geometry.h"

namespace espframe::native {

// The synthetic portraits have nonblack content right through the crop.
// Sample both outer edges and both sides of the centre seam before tint.
inline bool replay_pair_fills_panes(const uint16_t *panel, ScreenRotation rotation) {
  constexpr int rows[]{0, slide_height / 2, slide_height - 1};
  constexpr int edges[]{0, portrait_pair_width - 1, portrait_pair_width, slide_width - 1};
  for (const auto row : rows) {
    const auto column = panel_width - 1 - row;
    for (const auto x : edges) {
      const auto pixel = panel[panel_offset(x, column, rotation)];
      if ((pixel & 0xf800) <= 0x2000 && (pixel & 0x07e0) <= 0x0100 && (pixel & 0x001f) <= 4)
        return false;
    }
  }
  return true;
}

}  // namespace espframe::native
