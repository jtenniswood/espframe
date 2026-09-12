#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
using esp_err_t = int;
using nvs_handle_t = int;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, NVS_READONLY = 0, NVS_READWRITE = 1;
inline std::vector<uint8_t> identity_bytes;
inline bool fail_open = false, fail_read = false, fail_write = false, fail_commit = false;
inline int write_count = 0;
inline esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *handle) {
  if (std::string(ns) != "espframe_id" || fail_open) return 2;
  if (mode == NVS_READONLY && identity_bytes.empty()) return ESP_ERR_NVS_NOT_FOUND;
  *handle = 1;
  return ESP_OK;
}
inline void nvs_close(nvs_handle_t) {}
inline esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *data, size_t *length) {
  if (fail_read) return 2;
  if (identity_bytes.empty()) return ESP_ERR_NVS_NOT_FOUND;
  if (*length < identity_bytes.size()) return 2;
  *length = identity_bytes.size();
  std::memcpy(data, identity_bytes.data(), *length);
  return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *data, size_t length) {
  if (fail_write) return 2;
  ++write_count;
  identity_bytes.assign(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + length);
  return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t) { return fail_commit ? 2 : ESP_OK; }
