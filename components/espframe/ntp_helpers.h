#pragma once

#include "ntp_server_names.h"

#include <cstdio>

#include "esphome/core/log.h"

#if defined(USE_ESP32)
#include "esp_sntp.h"
#include "lwip/ip_addr.h"
#endif

inline bool espframe_apply_sntp_servers(
    const std::string &server1, const std::string &server2, const std::string &server3) {
  if (!espframe_is_valid_ntp_server(server1) ||
      !espframe_is_valid_ntp_server(server2) ||
      !espframe_is_valid_ntp_server(server3)) {
    ESP_LOGW("sntp", "NTP server names must be hostnames or IP addresses without spaces");
    return false;
  }

  const auto servers = espframe_normalize_ntp_servers(server1, server2, server3);

#if defined(USE_ESP32)
  static std::array<std::array<char, ESPFRAME_NTP_SERVER_MAX_LENGTH + 1>, ESPFRAME_NTP_SERVER_COUNT> storage = {};

  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);

  for (size_t i = 0; i < servers.size(); i++) {
    storage[i].fill('\0');
    if (servers[i].empty()) {
      ip_addr_t empty_addr;
      ip_addr_set_zero(&empty_addr);
      esp_sntp_setserver(i, &empty_addr);
    } else {
      std::snprintf(storage[i].data(), storage[i].size(), "%s", servers[i].c_str());
      esp_sntp_setservername(i, storage[i].data());
    }
  }

  esp_sntp_init();
  ESP_LOGI("sntp", "NTP servers applied: %s, %s, %s",
           servers[0].empty() ? "-" : servers[0].c_str(),
           servers[1].empty() ? "-" : servers[1].c_str(),
           servers[2].empty() ? "-" : servers[2].c_str());
  return true;
#else
  ESP_LOGW("sntp", "Runtime NTP server changes are only supported on ESP32");
  return false;
#endif
}
