#include "nvs.h"
#include "nvs_flash.h"
#include "settings.h"

#include <cassert>
#include <cstring>
#include <map>
#include <string>

namespace {

std::map<std::string, std::string> stored;
std::map<std::string, std::string> staged;

}  // namespace

esp_err_t nvs_flash_init_partition(const char *partition) {
  assert(std::string(partition) == "native_config");
  return ESP_OK;
}

esp_err_t nvs_open_from_partition(const char *partition, const char *name, int mode, nvs_handle_t *handle) {
  assert(std::string(partition) == "native_config" && std::string(name) == "player");
  *handle = mode;
  return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t, const char *key, char *out, size_t *size) {
  auto value = stored.find(key);
  if (value == stored.end())
    return 1;
  if (out != nullptr) {
    assert(*size >= value->second.size() + 1);
    std::memcpy(out, value->second.c_str(), value->second.size() + 1);
  }
  *size = value->second.size() + 1;
  return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) {
  assert(handle == NVS_READWRITE);
  staged[key] = value;
  return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
  assert(handle == NVS_READWRITE);
  for (const auto &[key, value] : staged)
    stored[key] = value;
  return ESP_OK;
}

void nvs_close(nvs_handle_t) { staged.clear(); }

int main() {
  using espframe::native::Settings;

  stored = {{"ssid", "Legacy"}, {"password", "legacy secret"}, {"immich_url", "http://legacy.local/"},
            {"immich_key", "legacy-key"}};
  Settings legacy;
  const auto legacy_store = stored;
  assert(legacy.read());
  assert(legacy.immich_url == "http://legacy.local/api" && legacy.slide_seconds == 15);
  assert(legacy.portrait_pairing);
  assert(legacy.web_password.empty());
  assert(!Settings::save_playback(R"({"immich_url":"http://new.local"})"));

  assert(Settings::save(R"({"ssid":"Photo Lab","password":"wifi secret",
                           "immich_url":"https://photos.test/api/","immich_key":"first-key",
                           "web_password":"only-settings","slide_seconds":20})"));
  Settings current;
  assert(current.read());
  assert(current.immich_url == "https://photos.test/api" && current.slide_seconds == 20);
  assert(current.web_password == "only-settings");
  auto before = stored;
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","slide_seconds":20.3})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://name@photos.test","slide_seconds":20})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","immich_key":"key with space"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","portrait_pairing":"false"})"));
  assert(stored == before);

  assert(Settings::save_playback(
      R"({"immich_url":"https://photos.test/family","immich_key":"","slide_seconds":35,"portrait_pairing":false})"));
  assert(current.read());
  assert(current.immich_url == "https://photos.test/family/api" && current.slide_seconds == 35);
  assert(!current.portrait_pairing);
  assert(current.ssid == "Photo Lab" && current.password == "wifi secret");
  assert(current.web_password == "only-settings" && current.immich_key == "first-key");
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","immich_key":"next-key","slide_seconds":2})"));
  assert(current.read());
  assert(current.immich_key == "next-key" && current.slide_seconds == 2);
  assert(!current.portrait_pairing);
  stored = legacy_store;
  assert(current.read());
  assert(current.web_password.empty() && current.slide_seconds == 15);
  assert(current.portrait_pairing);
}
