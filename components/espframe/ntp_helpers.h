#pragma once

#include <array>
#include <cctype>
#include <cstdio>
#include <string>

#include "esphome/core/log.h"

#if defined(USE_ESP32)
#include "esp_sntp.h"
#include "lwip/ip_addr.h"
#endif

static constexpr size_t ESPFRAME_NTP_SERVER_COUNT = 3;
static constexpr size_t ESPFRAME_NTP_SERVER_MAX_LENGTH = 253;

inline std::string espframe_trim_ntp_server(const std::string &value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) start++;

  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) end--;

  return value.substr(start, end - start);
}

inline bool espframe_is_valid_ntp_server(const std::string &value) {
  const std::string server = espframe_trim_ntp_server(value);
  if (server.empty()) return true;
  if (server.size() > ESPFRAME_NTP_SERVER_MAX_LENGTH) return false;

  for (char c : server) {
    if (std::isspace(static_cast<unsigned char>(c))) return false;
    if (c == ',' || c == '/' || c == '?' || c == '#') return false;
  }

  return true;
}

inline std::array<std::string, ESPFRAME_NTP_SERVER_COUNT> espframe_normalize_ntp_servers(
    const std::string &server1, const std::string &server2, const std::string &server3) {
  std::array<std::string, ESPFRAME_NTP_SERVER_COUNT> raw = {
      espframe_trim_ntp_server(server1),
      espframe_trim_ntp_server(server2),
      espframe_trim_ntp_server(server3),
  };

  std::array<std::string, ESPFRAME_NTP_SERVER_COUNT> normalized = {};
  size_t next = 0;
  for (const auto &server : raw) {
    if (!server.empty() && next < normalized.size()) normalized[next++] = server;
  }

  if (next == 0) {
    normalized[0] = "0.pool.ntp.org";
    normalized[1] = "1.pool.ntp.org";
    normalized[2] = "2.pool.ntp.org";
  }

  return normalized;
}

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
