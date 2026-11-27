#pragma once

#include "nvs.h"

#include <string>
#include <string_view>

namespace espframe_native_ota {

inline constexpr char migration_namespace[] = "espframe_move";
inline constexpr char migration_key[] = "v1_settings";
inline constexpr size_t migration_max_bytes = 4096;

// Refresh the snapshot on transport retries. After a reboot the bridge must
// independently prove a new rollback-capable ESPHome boot before starting.
inline bool stage_migration_snapshot(std::string_view snapshot) {
  if (snapshot.empty() || snapshot.size() > migration_max_bytes)
    return false;
  nvs_handle_t storage;
  if (nvs_open(migration_namespace, NVS_READWRITE, &storage) != ESP_OK)
    return false;
  const bool saved = nvs_set_blob(storage, migration_key, snapshot.data(), snapshot.size()) == ESP_OK &&
                     nvs_commit(storage) == ESP_OK;
  nvs_close(storage);
  return saved;
}

inline std::string read_migration_snapshot() {
  nvs_handle_t storage;
  if (nvs_open(migration_namespace, NVS_READONLY, &storage) != ESP_OK)
    return {};
  size_t length{};
  std::string snapshot;
  if (nvs_get_blob(storage, migration_key, nullptr, &length) == ESP_OK && length && length <= migration_max_bytes) {
    snapshot.resize(length);
    if (nvs_get_blob(storage, migration_key, snapshot.data(), &length) != ESP_OK ||
        snapshot.find('\0') != std::string::npos)
      snapshot.clear();
  }
  nvs_close(storage);
  return snapshot;
}

}  // namespace espframe_native_ota
