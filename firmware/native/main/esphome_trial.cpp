#include "esphome_trial.h"
#include "esphome_v1_layout.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#if !defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) || defined(CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK)
#error "Build the ESPHome swipe trial with application rollback and no eFuse anti-rollback"
#endif

namespace espframe::native {
namespace {

constexpr const char *tag = "esphome-trial";

using espframe_native_ota::is_app_in_ota_state;
using espframe_native_ota::is_v1_esphome_app;
using espframe_native_ota::other_esphome_v1_slot;

const esp_partition_t *previous_esphome(const esp_partition_t *running) {
  const auto *candidate = other_esphome_v1_slot(running);
  return is_v1_esphome_app(candidate) && is_app_in_ota_state(candidate, ESP_OTA_IMG_VALID) &&
         esp_ota_check_rollback_is_possible() ? candidate : nullptr;
}

void return_to_esphome(void *) { end_esphome_trial(); }

}  // namespace

void end_esphome_trial() {
  ESP_LOGI(tag, "Returning to the previous ESPHome firmware");
  if (const auto result = esp_ota_mark_app_invalid_rollback_and_reboot(); result != ESP_OK) {
    ESP_LOGE(tag, "Explicit rollback returned %s; rebooting with the trial unconfirmed", esp_err_to_name(result));
    esp_restart();
  }
}

bool start_esphome_trial(uint32_t seconds) {
  const auto *running = esp_ota_get_running_partition();
  const bool pending = is_app_in_ota_state(running, ESP_OTA_IMG_PENDING_VERIFY);
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
  if (esp_timer_create(&config, &timer) != ESP_OK || esp_timer_start_once(timer, seconds * 1'000'000ULL) != ESP_OK) {
    end_esphome_trial();
    return false;
  }
  ESP_LOGI(tag, "Native swipe trial active for %u seconds; ESPHome will return automatically",
           static_cast<unsigned>(seconds));
  return true;
}

}  // namespace espframe::native
