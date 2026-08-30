#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace esphome {
namespace remote_image {

// Keep the P4 accelerator's PSRAM footprint fixed after its first use. Immich's
// normal preview is comfortably below both limits; unusual or user-tuned large
// previews retain the software decoder fallback instead of growing and
// repeatedly replacing multi-megabyte DMA buffers.
static constexpr size_t P4_JPEG_MAX_INPUT_BYTES = 1 * 1024 * 1024;
static constexpr size_t P4_JPEG_MAX_DECODED_BYTES = 6 * 1024 * 1024;

struct P4JpegInfo {
  uint32_t width{0};
  uint32_t height{0};
  uint32_t padded_width{0};
  uint32_t padded_height{0};
  size_t decoded_bytes{0};
};

inline bool parse_p4_baseline_jpeg(const uint8_t *buffer, size_t size, P4JpegInfo *info) {
  if (buffer == nullptr || info == nullptr || size < 4 || buffer[0] != 0xFF || buffer[1] != 0xD8) {
    return false;
  }

  size_t offset = 2;
  while (offset + 1 < size) {
    while (offset < size && buffer[offset] != 0xFF) offset++;
    while (offset < size && buffer[offset] == 0xFF) offset++;
    if (offset >= size) return false;

    const uint8_t marker = buffer[offset++];
    if (marker == 0x00 || marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7)) continue;
    if (marker == 0xD9 || marker == 0xDA) return false;
    if (offset + 2 > size) return false;

    const size_t segment_size = (static_cast<size_t>(buffer[offset]) << 8) | buffer[offset + 1];
    if (segment_size < 2 || segment_size > size - offset) return false;

    if (marker == 0xC0) {
      // SOF0: length, precision, height, width, component count, then three
      // bytes per component. P4 supports 8-bit, three-component baseline JPEG
      // with 4:4:4, 4:2:2, or 4:2:0 luma sampling.
      if (segment_size < 17 || buffer[offset + 2] != 8 || buffer[offset + 7] != 3) return false;
      const uint32_t height = (static_cast<uint32_t>(buffer[offset + 3]) << 8) | buffer[offset + 4];
      const uint32_t width = (static_cast<uint32_t>(buffer[offset + 5]) << 8) | buffer[offset + 6];
      if (width == 0 || height == 0) return false;

      uint32_t mcu_width = 0;
      uint32_t mcu_height = 0;
      switch (buffer[offset + 9]) {
        case 0x11:
          mcu_width = 8;
          mcu_height = 8;
          break;
        case 0x21:
          mcu_width = 16;
          mcu_height = 8;
          break;
        case 0x22:
          mcu_width = 16;
          mcu_height = 16;
          break;
        default:
          return false;
      }
      if (buffer[offset + 12] != 0x11 || buffer[offset + 15] != 0x11) return false;

      const uint64_t padded_width = (static_cast<uint64_t>(width) + mcu_width - 1) / mcu_width * mcu_width;
      const uint64_t padded_height = (static_cast<uint64_t>(height) + mcu_height - 1) / mcu_height * mcu_height;
      const uint64_t decoded_bytes = padded_width * padded_height * 2;
      if (padded_width > std::numeric_limits<uint32_t>::max() ||
          padded_height > std::numeric_limits<uint32_t>::max() ||
          decoded_bytes > std::numeric_limits<size_t>::max()) {
        return false;
      }

      info->width = width;
      info->height = height;
      info->padded_width = static_cast<uint32_t>(padded_width);
      info->padded_height = static_cast<uint32_t>(padded_height);
      info->decoded_bytes = static_cast<size_t>(decoded_bytes);
      return true;
    }

    // Any other start-of-frame marker describes a progressive, lossless, or
    // extended JPEG mode unsupported by the ESP32-P4 hardware block.
    if (marker >= 0xC1 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
      return false;
    }
    offset += segment_size;
  }
  return false;
}

inline bool p4_jpeg_fits_workspace(const P4JpegInfo &info, size_t input_bytes) {
  return input_bytes > 0 && input_bytes <= P4_JPEG_MAX_INPUT_BYTES &&
         info.decoded_bytes > 0 && info.decoded_bytes <= P4_JPEG_MAX_DECODED_BYTES;
}

}  // namespace remote_image
}  // namespace esphome
