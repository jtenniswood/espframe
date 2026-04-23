#include "esphome/core/defines.h"

#ifdef USE_ESP32_VARIANT_ESP32P4

#include "driver/ppa.h"
#include "esp_err.h"
#include "esphome/core/log.h"

namespace {
static const char *const TAG = "espframe.ppa";
}

extern "C" esp_err_t __wrap_ppa_register_client(const ppa_client_config_t *config, ppa_client_handle_t *ret_client) {
  (void) config;
  if (ret_client != nullptr) {
    *ret_client = nullptr;
  }
  ESP_LOGW(TAG, "PPA disabled for LVGL rotation; using CPU software rotation");
  return ESP_ERR_NOT_SUPPORTED;
}

#endif  // USE_ESP32_VARIANT_ESP32P4
