#pragma once

#include "slide_geometry.h"

#include <algorithm>
#include <cstdint>

namespace espframe::native {

struct TouchReport {
  int x{};
  int y{};
  uint8_t contacts{};
  bool valid{};
  uint32_t sequence{};
  int64_t sampled_us{};
};

inline void map_gsl_touch(TouchReport &report, int sensor_x, int sensor_y, ScreenRotation rotation) {
  // The Guition sensor's Y axis is the panel row, and its X axis is the
  // reversed panel column. Convert through the same axes as the renderer.
  const Viewport viewport{rotation};
  const int row = std::clamp(sensor_y, 0, panel_height - 1);
  const int column = panel_width - 1 - std::clamp(sensor_x, 0, panel_width - 1);
  report.x = oriented_axis(viewport.portrait ? column : row, viewport.width, rotation);
  const int y = viewport.portrait ? row : panel_width - 1 - column;
  report.y = oriented_axis(y, viewport.height, rotation);
}

}  // namespace espframe::native
