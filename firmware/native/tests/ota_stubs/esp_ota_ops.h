#pragma once

#include <cstddef>
#include <cstdint>

using esp_err_t = int;
using esp_ota_handle_t = unsigned;
using esp_ota_img_states_t = int;

inline constexpr esp_err_t ESP_OK = 0;
inline constexpr int ESP_OTA_IMG_PENDING_VERIFY = 1;
inline constexpr size_t OTA_WITH_SEQUENTIAL_WRITES = 0xfffffffe;

struct esp_partition_t {
  uint32_t address;
  size_t size;
};

esp_err_t esp_ota_begin(const esp_partition_t *, size_t, esp_ota_handle_t *);
esp_err_t esp_ota_write(esp_ota_handle_t, const void *, size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *, esp_ota_img_states_t *);
esp_err_t esp_ota_mark_app_valid_cancel_rollback();
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot();
const esp_partition_t *esp_ota_get_running_partition();
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *);
const esp_partition_t *esp_ota_get_boot_partition();
