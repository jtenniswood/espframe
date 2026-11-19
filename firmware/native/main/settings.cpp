#include "settings.h"

#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace espframe::native {
namespace {

constexpr const char *partition = "native_config";
constexpr const char *store_namespace = "player";
constexpr const char *settings_key = "settings_v1";

bool load_string(nvs_handle_t storage, const char *key, std::string &value, size_t maximum) {
  size_t size = 0;
  if (nvs_get_str(storage, key, nullptr, &size) != ESP_OK || size == 0 || size > maximum + 1)
    return false;
  value.resize(size);
  if (nvs_get_str(storage, key, value.data(), &size) != ESP_OK)
    return false;
  value.resize(size - 1);
  return true;
}

bool normalize(Settings &settings) {
  if (settings.ssid.empty() || settings.ssid.size() > 32 || settings.password.size() > 64 ||
      settings.immich_key.empty() || settings.immich_key.size() > 256 ||
      std::any_of(settings.immich_key.begin(), settings.immich_key.end(),
                  [](unsigned char c) { return c <= 32 || c >= 127; }))
    return false;
  auto &url = settings.immich_url;
  const size_t host = url.starts_with("https://") ? 8 : url.starts_with("http://") ? 7 : 0;
  if (host == 0 || host >= url.size() || url.size() > 256 || url[host] == '/' ||
      url.find_first_of("@?#\\") != std::string::npos ||
      std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 32 || c >= 127; }))
    return false;
  while (url.back() == '/')
    url.pop_back();
  if (!url.ends_with("/api"))
    url += "/api";
  return true;
}

bool parse(const char *json, Settings &settings) {
  if (json == nullptr || std::strlen(json) > Settings::max_json_size)
    return false;
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_ParseWithOpts(json, nullptr, true),
                                                           cJSON_Delete);
  if (!cJSON_IsObject(root.get()))
    return false;
  auto get = [&](const char *name, std::string &value) {
    const char *field = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root.get(), name));
    if (field == nullptr)
      return false;
    value = field;
    return true;
  };
  return get("ssid", settings.ssid) && get("password", settings.password) &&
         get("immich_url", settings.immich_url) && get("immich_key", settings.immich_key) && normalize(settings);
}

}  // namespace

bool Settings::read() {
  if (nvs_flash_init_partition(partition) != ESP_OK)
    return false;
  nvs_handle_t storage;
  if (nvs_open_from_partition(partition, store_namespace, NVS_READONLY, &storage) != ESP_OK)
    return false;
  std::string json;
  const bool ok = load_string(storage, settings_key, json, max_json_size)
                      ? parse(json.c_str(), *this)
                      : load_string(storage, "ssid", this->ssid, 32) &&
                            load_string(storage, "password", this->password, 64) &&
                            load_string(storage, "immich_url", this->immich_url, 256) &&
                            load_string(storage, "immich_key", this->immich_key, 256) && normalize(*this);
  nvs_close(storage);
  return ok;
}

bool Settings::save(const char *json) {
  Settings candidate;
  if (!parse(json, candidate) || nvs_flash_init_partition(partition) != ESP_OK)
    return false;
  nvs_handle_t storage;
  if (nvs_open_from_partition(partition, store_namespace, NVS_READWRITE, &storage) != ESP_OK)
    return false;
  const bool ok = nvs_set_str(storage, settings_key, json) == ESP_OK && nvs_commit(storage) == ESP_OK;
  nvs_close(storage);
  return ok;
}

}  // namespace espframe::native
