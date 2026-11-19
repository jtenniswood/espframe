#pragma once

#include <cstddef>
#include <cstdint>

using esp_err_t = int;
using esp_ota_handle_t = unsigned;
using esp_ota_img_states_t = int;
using esp_partition_type_t = int;
using esp_partition_subtype_t = int;

inline constexpr esp_err_t ESP_OK = 0;
inline constexpr int ESP_OTA_IMG_PENDING_VERIFY = 1;
inline constexpr int ESP_OTA_IMG_VALID = 2;
inline constexpr int ESP_OTA_IMG_INVALID = 3;
inline constexpr int ESP_OTA_IMG_ABORTED = 4;
inline constexpr int ESP_PARTITION_TYPE_APP = 0;
inline constexpr int ESP_PARTITION_TYPE_DATA = 1;
inline constexpr int ESP_PARTITION_SUBTYPE_APP_FACTORY = 0;
inline constexpr int ESP_PARTITION_SUBTYPE_APP_OTA_0 = 16;
inline constexpr int ESP_PARTITION_SUBTYPE_APP_OTA_1 = 17;
inline constexpr int ESP_PARTITION_SUBTYPE_DATA_OTA = 0;
inline constexpr int ESP_PARTITION_SUBTYPE_DATA_PHY = 1;
inline constexpr int ESP_PARTITION_SUBTYPE_DATA_NVS = 2;
inline constexpr size_t OTA_WITH_SEQUENTIAL_WRITES = 0xfffffffe;

struct esp_partition_t {
  uint32_t address;
  size_t size;
  bool encrypted{};
};

struct esp_app_desc_t;

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
const esp_partition_t *esp_partition_find_first(esp_partition_type_t, esp_partition_subtype_t, const char *);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *, esp_app_desc_t *);
bool esp_ota_check_rollback_is_possible();
