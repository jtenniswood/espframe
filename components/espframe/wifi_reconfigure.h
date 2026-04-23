#pragma once

#include "esphome/core/log.h"

#ifdef USE_WIFI
#ifdef USE_WIFI_AP

// ESPHome does not currently expose a public helper for "start fallback AP and
// captive portal now". Keep the access hack local to this header so YAML
// lambdas can use a small, stable wrapper.
#define private public
#define protected public
#include "esphome/components/captive_portal/captive_portal.h"
#include "esphome/components/wifi/wifi_component.h"
#undef protected
#undef private

#endif
#endif

namespace esphome {
namespace espframe {

inline void force_wifi_reconfigure_mode() {
#ifdef USE_WIFI
#ifdef USE_WIFI_AP
  static const char *const TAG = "wifi.reconfig";

  auto *wifi = wifi::global_wifi_component;
  if (wifi == nullptr) {
    ESP_LOGW(TAG, "WiFi component not available; cannot start WiFi setup mode");
    return;
  }

  wifi->clear_sta();
  wifi->setup_ap_config_();

#ifdef USE_CAPTIVE_PORTAL
  auto *portal = captive_portal::global_captive_portal;
  if (portal != nullptr && !portal->is_active()) {
    portal->start();
  }
#endif

  ESP_LOGI(TAG, "WiFi setup mode active");
#endif
#endif
}

inline bool is_wifi_reconfigure_portal_active() {
#ifdef USE_WIFI
#ifdef USE_WIFI_AP
#ifdef USE_CAPTIVE_PORTAL
  auto *portal = captive_portal::global_captive_portal;
  return portal != nullptr && portal->is_active();
#else
  return false;
#endif
#else
  return false;
#endif
#else
  return false;
#endif
}

}  // namespace espframe
}  // namespace esphome
