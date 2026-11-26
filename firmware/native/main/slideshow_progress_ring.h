#pragma once

#include "slide_geometry.h"

#include <array>
#include <cmath>

namespace espframe::native {

inline void paint_slideshow_progress_ring(uint16_t *frame, const Viewport &viewport, int progress_tenths) {
  constexpr int diameter = 32;
  constexpr int margin = 24;
  constexpr float outer_radius = 16;
  constexpr float inner_radius = 13;
  struct Pixel {
    uint16_t clockwise_tenths{};
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
        const float angle = std::atan2(dx, -dy) * (1800.f / 3.14159265359f);
        pixels[y * diameter + x] = {static_cast<uint16_t>(angle < 0 ? angle + 3600.f : angle),
                                    static_cast<uint8_t>(coverage * 255)};
      }
    }
    return pixels;
  }();

  for (int y = 0; y < diameter; ++y) {
    for (int x = 0; x < diameter; ++x) {
      const auto &pixel = mask[y * diameter + x];
      if (pixel.coverage == 0)
        continue;
      // Four degrees at the middle of the stroke is about one pixel.
      constexpr int edge_tenths = 40;
      const unsigned fill = progress_tenths == 0 ? 0 : progress_tenths == 3600 ? 255
          : std::clamp((progress_tenths - pixel.clockwise_tenths + edge_tenths / 2) * 255 / edge_tenths, 0, 255);
      const unsigned white_coverage = (pixel.coverage * fill + 127) / 255;
      constexpr unsigned track_opacity = 51;  // 20% opaque black.
      const unsigned black_coverage = ((pixel.coverage - white_coverage) * track_opacity + 127) / 255;
      const auto photo_coverage = 255 - white_coverage - black_coverage;
      auto &background = frame[viewport.offset(viewport.width - margin - diameter + x, margin + y)];
      const auto blend = [white_coverage, photo_coverage](unsigned channel, unsigned white) {
        return (channel * photo_coverage + white * white_coverage + 127) / 255;
      };
      background = static_cast<uint16_t>((blend(background >> 11, 31) << 11) |
                                        (blend((background >> 5) & 63, 63) << 5) |
                                        blend(background & 31, 31));
    }
  }
}

}  // namespace espframe::native
