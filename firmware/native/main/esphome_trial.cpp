#include "esphome_trial.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include <cstring>

#if !defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) || defined(CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK)
#error "Build the ESPHome swipe trial with application rollback and no eFuse anti-rollback"
#endif

namespace espframe::native {
namespace {

constexpr const char *tag = "esphome-trial";
constexpr uint64_t duration_us = 180'000'000;

const esp_partition_t *matching_partition(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                          uint32_t address, size_t size) {
  const auto *partition = esp_partition_find_first(type, subtype, nullptr);
  return partition && partition->address == address && partition->size == size && !partition->encrypted
             ? partition : nullptr;
}

const esp_partition_t *previous_esphome(const esp_partition_t *running) {
  const auto *slot0 = matching_partition(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, 0x10000, 0x7c0000);
  const auto *slot1 = matching_partition(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, 0x7d0000, 0x7c0000);
  if (!running || !slot0 || !slot1 || (running->address != slot0->address && running->address != slot1->address) ||
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr) ||
      !matching_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, 0x9000, 0x2000) ||
      !matching_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_PHY, 0xb000, 0x1000) ||
      !matching_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, 0xf90000, 0x70000))
    return nullptr;

  const auto *candidate = running->address == slot0->address ? slot1 : slot0;
  esp_app_desc_t app{};
  esp_ota_img_states_t state;
  static constexpr char v1_project[] = "immich-frame-10inch";
  return esp_ota_get_partition_description(candidate, &app) == ESP_OK &&
         std::strncmp(app.project_name, v1_project, sizeof(app.project_name)) == 0 &&
         esp_ota_get_state_partition(candidate, &state) == ESP_OK && state == ESP_OTA_IMG_VALID &&
         esp_ota_check_rollback_is_possible() ? candidate : nullptr;
}

void return_to_esphome(void *) {
  ESP_LOGI(tag, "Returning to the previous ESPHome firmware");
  if (const auto result = esp_ota_mark_app_invalid_rollback_and_reboot(); result != ESP_OK) {
    ESP_LOGE(tag, "Explicit rollback returned %s; rebooting with the trial unconfirmed", esp_err_to_name(result));
    esp_restart();
  }
}

}  // namespace

bool start_esphome_trial() {
  const auto *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  const bool pending = running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
                       state == ESP_OTA_IMG_PENDING_VERIFY;
  const auto *previous = previous_esphome(running);
  if (!previous) {
    ESP_LOGE(tag, "Swipe trial refused: no verified ESPHome V1 rollback slot on the expected partition table");
    if (pending)
      esp_restart();
    return false;
  }
  if (!pending) {
    ESP_LOGE(tag, "Swipe trial refused: bootloader did not mark this boot as pending verification");
    if (esp_ota_set_boot_partition(previous) == ESP_OK)
      esp_restart();
    return false;
  }

  esp_timer_handle_t timer{};
  esp_timer_create_args_t config{};
  config.callback = return_to_esphome;
  config.name = "esphome-trial";
  if (esp_timer_create(&config, &timer) != ESP_OK || esp_timer_start_once(timer, duration_us) != ESP_OK) {
    return_to_esphome(nullptr);
    return false;
  }
  ESP_LOGI(tag, "Native swipe trial active for three minutes; ESPHome will return automatically");
  return true;
}

}  // namespace espframe::native
