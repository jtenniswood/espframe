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
  // Match the Guition touchscreen mounting used by ESPHome. The sensor's
  // Y axis is landscape X, and its X axis is landscape Y.
  report.x = oriented_axis(std::clamp(sensor_y, 0, slide_width - 1), slide_width, rotation);
  report.y = oriented_axis(std::clamp(sensor_x, 0, slide_height - 1), slide_height, rotation);
}

}  // namespace espframe::native
