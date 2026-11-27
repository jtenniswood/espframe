#pragma once

#include "esphome_v1_layout.h"
#include "legacy_migration_storage.h"

#include <array>

namespace espframe_native_ota {

inline bool bridge_rollback_proof(bool save) {
  const auto &running = *esp_app_get_description();
  nvs_handle_t storage;
  if (nvs_open(migration_namespace, save ? NVS_READWRITE : NVS_READONLY, &storage) != ESP_OK)
    return false;
  constexpr char key[] = "bridge_elf";
  bool matched;
  if (save) {
    matched = nvs_set_blob(storage, key, running.app_elf_sha256, sizeof(running.app_elf_sha256)) == ESP_OK &&
              nvs_commit(storage) == ESP_OK;
  } else {
    std::array<uint8_t, sizeof(running.app_elf_sha256)> digest{};
    size_t length = digest.size();
    matched = nvs_get_blob(storage, key, digest.data(), &length) == ESP_OK &&
              length == digest.size() && std::memcmp(digest.data(), running.app_elf_sha256, length) == 0;
  }
  nvs_close(storage);
  return matched;
}

inline bool read_bridge_rollback_proof() { return bridge_rollback_proof(false); }
inline bool save_bridge_rollback_proof() { return bridge_rollback_proof(true); }

inline bool rejected_migration(const esp_partition_t *partition, std::string_view candidate) {
  if (!is_app_in_ota_state(partition, ESP_OTA_IMG_INVALID) && !is_app_in_ota_state(partition, ESP_OTA_IMG_ABORTED))
    return false;
  esp_app_desc_t app{};
  if (candidate.size() != 2 * sizeof(app.app_elf_sha256) ||
      esp_ota_get_partition_description(partition, &app) != ESP_OK)
    return false;
  constexpr char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(app.app_elf_sha256); ++i) {
    const auto value = app.app_elf_sha256[i];
    if (candidate[2 * i] != hex[value >> 4] || candidate[2 * i + 1] != hex[value & 15])
      return false;
  }
  return true;
}

}  // namespace espframe_native_ota
