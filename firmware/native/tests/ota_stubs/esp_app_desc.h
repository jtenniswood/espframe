#pragma once

#include <cstdint>

inline constexpr uint32_t ESP_APP_DESC_MAGIC_WORD = 0xABCD5432;

// Only expose fields read by the player. The test reads a real ESP-IDF image.
struct esp_app_desc_t {
  uint32_t magic_word;
  uint8_t preceding_fields[44];
  char project_name[32];
  uint8_t before_hash[64];
  uint8_t app_elf_sha256[32];
  uint8_t following_fields[80];
};
