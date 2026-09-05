#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace remote_image {

// BT.601 luma with integer rounding, preserving black, white, and neutral gray.
inline uint8_t mono_luminance(uint8_t r, uint8_t g, uint8_t b) {
  return (299U * r + 587U * g + 114U * b + 500U) / 1000U;
}

inline uint16_t mono_rgb565(uint16_t pixel) {
  const uint8_t r5 = (pixel >> 11) & 31;
  const uint8_t g6 = (pixel >> 5) & 63;
  const uint8_t b5 = pixel & 31;
  const uint8_t gray = mono_luminance((r5 << 3) | (r5 >> 2),
                                     (g6 << 2) | (g6 >> 4),
                                     (b5 << 3) | (b5 >> 2));
  return ((gray & 0xF8) << 8) | ((gray & 0xFC) << 3) | (gray >> 3);
}

// Byte access supports either display byte order and unaligned decoder rows.
inline void mono_rgb565_bytes(uint8_t *pixels, size_t count, bool big_endian) {
  for (size_t i = 0; i < count; i++, pixels += 2) {
    const uint16_t pixel = big_endian ? (pixels[0] << 8) | pixels[1]
                                      : (pixels[1] << 8) | pixels[0];
    const uint16_t gray = mono_rgb565(pixel);
    pixels[0] = big_endian ? gray >> 8 : gray & 0xFF;
    pixels[1] = big_endian ? gray & 0xFF : gray >> 8;
  }
}

}  // namespace remote_image
}  // namespace esphome
