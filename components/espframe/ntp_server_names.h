#pragma once

#include <array>
#include <cctype>
#include <cstddef>
#include <string>

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
