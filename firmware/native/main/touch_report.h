#pragma once

#include "slide_geometry.h"

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

inline void orient_touch(TouchReport &report, ScreenRotation rotation) {
  report.x = oriented_axis(report.x, slide_width, rotation);
  report.y = oriented_axis(report.y, slide_height, rotation);
}

}  // namespace espframe::native
