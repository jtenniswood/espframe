#pragma once

#include "slide_geometry.h"

#include <array>
#include <cmath>

namespace espframe::native {

inline void paint_slideshow_progress_ring(uint16_t *frame, const Viewport &viewport, int progress_degrees) {
  constexpr int diameter = 32;
  constexpr int margin = 24;
  constexpr float outer_radius = 16;
  constexpr float inner_radius = 13;
  struct Pixel {
    float clockwise_degrees{};
    uint8_t coverage{};
  };
  static const auto mask = [] {
    std::array<Pixel, diameter * diameter> pixels{};
    for (int y = 0; y < diameter; ++y) {
      for (int x = 0; x < diameter; ++x) {
        const float dx = x + 0.5f - outer_radius;
        const float dy = y + 0.5f - outer_radius;
        const float radius = std::hypot(dx, dy);
        const float coverage = std::clamp(std::min(radius - inner_radius, outer_radius - radius) + 0.5f, 0.f, 1.f);
        const float degrees = std::atan2(dx, -dy) * (180.f / 3.14159265359f);
        pixels[y * diameter + x] = {degrees < 0 ? degrees + 360.f : degrees, static_cast<uint8_t>(coverage * 255)};
      }
    }
    return pixels;
  }();

  for (int y = 0; y < diameter; ++y) {
    for (int x = 0; x < diameter; ++x) {
      const auto &pixel = mask[y * diameter + x];
      if (pixel.coverage == 0)
        continue;
      const unsigned opacity = pixel.clockwise_degrees < progress_degrees ? pixel.coverage : (pixel.coverage + 1) / 2;
      auto &background = frame[viewport.offset(viewport.width - margin - diameter + x, margin + y)];
      const auto blend = [opacity](unsigned channel, unsigned white) {
        return channel + ((white - channel) * opacity + 127) / 255;
      };
      background = static_cast<uint16_t>((blend(background >> 11, 31) << 11) |
                                        (blend((background >> 5) & 63, 63) << 5) |
                                        blend(background & 31, 31));
    }
  }
}

}  // namespace espframe::native
