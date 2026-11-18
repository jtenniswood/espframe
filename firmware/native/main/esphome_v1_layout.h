#pragma once

#include "esp_app_desc.h"
#include "esp_ota_ops.h"

#include <cstring>

namespace espframe_native_ota {

inline const esp_partition_t *matching_partition(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                 uint32_t address, size_t size) {
  const auto *partition = esp_partition_find_first(type, subtype, nullptr);
  return partition && partition->address == address && partition->size == size && !partition->encrypted
             ? partition : nullptr;
}

inline const esp_partition_t *other_esphome_v1_slot(const esp_partition_t *running) {
  const auto *slot0 = matching_partition(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, 0x10000, 0x7c0000);
  const auto *slot1 = matching_partition(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, 0x7d0000, 0x7c0000);
  if (!running || !slot0 || !slot1 || (running->address != slot0->address && running->address != slot1->address) ||
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr) ||
      !matching_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, 0x9000, 0x2000) ||
      !matching_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_PHY, 0xb000, 0x1000) ||
      !matching_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, 0xf90000, 0x70000))
    return nullptr;
  return running->address == slot0->address ? slot1 : slot0;
}

inline bool is_v1_esphome_app(const esp_partition_t *partition) {
  esp_app_desc_t app{};
  return partition && esp_ota_get_partition_description(partition, &app) == ESP_OK &&
         std::strncmp(app.project_name, "immich-frame-10inch", sizeof(app.project_name)) == 0;
}

inline bool is_app_in_ota_state(const esp_partition_t *partition, esp_ota_img_states_t expected) {
  esp_ota_img_states_t state;
  return partition && esp_ota_get_state_partition(partition, &state) == ESP_OK && state == expected;
}

inline const char *esphome_v1_trial_readiness(bool rollback_observed) {
  const auto *running = esp_ota_get_running_partition();
  const auto *other = other_esphome_v1_slot(running);
  if (!other || esp_ota_get_next_update_partition(nullptr) != other)
    return "Unsupported V1 partition layout";
  if (!is_v1_esphome_app(running))
    return "Unexpected ESPHome app";
  if (!rollback_observed)
    return "Bootloader rollback unverified";
  if (esp_ota_get_boot_partition() != running)
    return "Another boot is staged";
  if (!is_app_in_ota_state(running, ESP_OTA_IMG_VALID))
    return "Waiting for ESPHome confirmation";
  return "Ready";
}

}  // namespace espframe_native_ota
