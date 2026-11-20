#pragma once

#include <cstdint>

inline constexpr uint8_t ESP_IMAGE_HEADER_MAGIC = 0xe9;
inline constexpr uint16_t ESP_CHIP_ID_ESP32P4 = 0x12;

struct esp_image_header_t {
  uint8_t magic;
  uint8_t preceding_fields[11];
  uint16_t chip_id;
  uint8_t following_fields[9];
  uint8_t hash_appended;
};

struct esp_image_segment_header_t {
  uint32_t load_addr;
  uint32_t data_len;
};
