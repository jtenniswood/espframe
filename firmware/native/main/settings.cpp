#include "settings.h"

#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

namespace espframe::native {
namespace {

constexpr const char *partition = "native_config";
constexpr const char *store_namespace = "player";
constexpr const char *settings_key = "settings_v1";
std::mutex storage_mutex;

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
      settings.web_password.size() > 64 || (!settings.web_password.empty() && settings.web_password.size() < 8) ||
      settings.slide_seconds < 2 || settings.slide_seconds > 86'400 ||
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

using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

Json parse(const char *json) {
  if (json == nullptr || std::strlen(json) > Settings::max_json_size)
    return {nullptr, cJSON_Delete};
  return {cJSON_ParseWithOpts(json, nullptr, true), cJSON_Delete};
}

bool read_field(const cJSON *root, const char *name, std::string &value, bool optional = false) {
  const cJSON *field = cJSON_GetObjectItemCaseSensitive(root, name);
  if (!field && optional)
    return true;
  const char *text = cJSON_GetStringValue(field);
  if (!text)
    return false;
  value = text;
  return true;
}

template <typename Integer>
bool read_integer(const cJSON *root, const char *name, int minimum, int maximum, Integer &value) {
  const auto *field = cJSON_GetObjectItemCaseSensitive(root, name);
  if (!field)
    return true;
  if (!cJSON_IsNumber(field) || field->valueint < minimum || field->valueint > maximum ||
      field->valueint != field->valuedouble)
    return false;
  value = field->valueint;
  return true;
}

bool read_flag(const cJSON *root, const char *name, bool &enabled) {
  const auto *field = cJSON_GetObjectItemCaseSensitive(root, name);
  if (!field)
    return true;
  if (!cJSON_IsBool(field))
    return false;
  enabled = cJSON_IsTrue(field);
  return true;
}

bool read_clock(const cJSON *root, ClockSettings &clock) {
  return read_flag(root, "show_clock", clock.show) && read_field(root, "clock_format", clock.format, true) &&
         (clock.format == "24 Hour" || clock.format == "12 Hour") &&
         read_field(root, "timezone", clock.timezone, true) && clock_timezone(clock.timezone) != nullptr;
}

bool read_schedule(const cJSON *root, ScreenScheduleSettings &schedule) {
  return read_flag(root, "schedule_enabled", schedule.enabled) &&
         read_integer(root, "schedule_on_hour", 0, 23, schedule.on_hour) &&
         read_integer(root, "schedule_off_hour", 0, 23, schedule.off_hour) &&
         read_integer(root, "schedule_wake_timeout", 10, 3600, schedule.wake_seconds);
}

bool read_brightness(const cJSON *root, Settings &settings) {
  if (!read_integer(root, "brightness_day", 10, 100, settings.brightness_day))
    return false;
  if (cJSON_HasObjectItem(root, "brightness_night")) {
    uint8_t night{};
    if (!read_integer(root, "brightness_night", 10, 100, night))
      return false;
    settings.brightness_night = night;
  }
  return true;
}

bool read_display_mode(const cJSON *root, DisplayMode &mode) {
  const auto *field = cJSON_GetObjectItemCaseSensitive(root, "display_mode");
  if (!field)
    return true;
  const char *text = cJSON_GetStringValue(field);
  if (!text)
    return false;
  const std::string_view value{text};
  if (value == "Fill")
    mode = DisplayMode::Fill;
  else if (value == "Fit")
    mode = DisplayMode::Fit;
  else
    return false;
  return true;
}

bool read_rotation(const cJSON *root, ScreenRotation &rotation) {
  const auto *field = cJSON_GetObjectItemCaseSensitive(root, "screen_rotation");
  if (!field)
    return true;
  const char *text = cJSON_GetStringValue(field);
  if (!text)
    return false;
  const std::string_view value{text};
  if (value == "0")
    rotation = ScreenRotation::Normal;
  else if (value == "180")
    rotation = ScreenRotation::UpsideDown;
  else
    return false;
  return true;
}

bool read_source(const cJSON *root, PhotoSource &source) {
  std::string mode = photo_source_name(source.mode);
  if (!read_field(root, "photo_source", mode, true) || !read_field(root, "album_id", source.album_id, true))
    return false;
  for (char &c : source.album_id) {
    if (c >= 'A' && c <= 'F')
      c = static_cast<char>(c + ('a' - 'A'));
  }
  if ((!source.album_id.empty() || mode == "album") && !is_immich_id(source.album_id))
    return false;
  if (mode == "all")
    source.mode = PhotoSourceMode::Library;
  else if (mode == "favorites")
    source.mode = PhotoSourceMode::Favorites;
  else if (mode == "album")
    source.mode = PhotoSourceMode::Album;
  else
    return false;
  return true;
}

bool read_playback(const cJSON *root, Settings &settings) {
  return read_integer(root, "slide_seconds", 2, 86'400, settings.slide_seconds) &&
         read_flag(root, "portrait_pairing", settings.portrait_pairing) &&
         read_display_mode(root, settings.display_mode) && read_rotation(root, settings.screen_rotation) &&
         read_source(root, settings.photo_source) &&
         read_clock(root, settings.clock) && read_brightness(root, settings) &&
         read_schedule(root, settings.schedule);
}

bool parse_all(const char *json, Settings &settings) {
  const auto root = parse(json);
  return cJSON_IsObject(root.get()) && read_field(root.get(), "ssid", settings.ssid) &&
         read_field(root.get(), "password", settings.password) &&
         read_field(root.get(), "immich_url", settings.immich_url) &&
         read_field(root.get(), "immich_key", settings.immich_key) &&
         read_field(root.get(), "web_password", settings.web_password, true) &&
         read_playback(root.get(), settings) && normalize(settings);
}

bool load(Settings &settings) {
  if (nvs_flash_init_partition(partition) != ESP_OK)
    return false;
  nvs_handle_t storage;
  if (nvs_open_from_partition(partition, store_namespace, NVS_READONLY, &storage) != ESP_OK)
    return false;
  std::string json;
  const bool ok = load_string(storage, settings_key, json, Settings::max_json_size)
                      ? parse_all(json.c_str(), settings)
                      : load_string(storage, "ssid", settings.ssid, 32) &&
                            load_string(storage, "password", settings.password, 64) &&
                            load_string(storage, "immich_url", settings.immich_url, 256) &&
                            load_string(storage, "immich_key", settings.immich_key, 256) && normalize(settings);
  nvs_close(storage);
  return ok;
}

bool write(const char *json) {
  if (std::strlen(json) > Settings::max_json_size || nvs_flash_init_partition(partition) != ESP_OK)
    return false;
  nvs_handle_t storage;
  if (nvs_open_from_partition(partition, store_namespace, NVS_READWRITE, &storage) != ESP_OK)
    return false;
  const bool ok = nvs_set_str(storage, settings_key, json) == ESP_OK && nvs_commit(storage) == ESP_OK;
  nvs_close(storage);
  return ok;
}

}  // namespace

bool Settings::read() {
  Settings candidate;
  const std::lock_guard lock(storage_mutex);
  if (!load(candidate))
    return false;
  *this = std::move(candidate);
  return true;
}

bool Settings::save(const char *json) {
  Settings candidate;
  const std::lock_guard lock(storage_mutex);
  return parse_all(json, candidate) && write(json);
}

bool Settings::save_playback(const char *json) {
  const auto update = parse(json);
  std::string replacement_key;
  const std::lock_guard lock(storage_mutex);
  Settings settings;
  if (!cJSON_IsObject(update.get()) || !load(settings) || settings.web_password.empty() ||
      !read_field(update.get(), "immich_url", settings.immich_url) ||
      !read_field(update.get(), "immich_key", replacement_key, true) ||
      !read_playback(update.get(), settings))
    return false;
  if (!replacement_key.empty())
    settings.immich_key = replacement_key;
  const Json updated(cJSON_CreateObject(), cJSON_Delete);
  if (!updated || !cJSON_AddStringToObject(updated.get(), "ssid", settings.ssid.c_str()) ||
      !cJSON_AddStringToObject(updated.get(), "password", settings.password.c_str()) ||
      !cJSON_AddStringToObject(updated.get(), "immich_url", settings.immich_url.c_str()) ||
      !cJSON_AddStringToObject(updated.get(), "immich_key", settings.immich_key.c_str()) ||
      !cJSON_AddStringToObject(updated.get(), "web_password", settings.web_password.c_str()) ||
      !cJSON_AddNumberToObject(updated.get(), "slide_seconds", settings.slide_seconds) ||
      !cJSON_AddNumberToObject(updated.get(), "brightness_day", settings.brightness_day) ||
      (settings.brightness_night && !cJSON_AddNumberToObject(updated.get(), "brightness_night", *settings.brightness_night)) ||
      !cJSON_AddBoolToObject(updated.get(), "schedule_enabled", settings.schedule.enabled) ||
      !cJSON_AddNumberToObject(updated.get(), "schedule_on_hour", settings.schedule.on_hour) ||
      !cJSON_AddNumberToObject(updated.get(), "schedule_off_hour", settings.schedule.off_hour) ||
      !cJSON_AddNumberToObject(updated.get(), "schedule_wake_timeout", settings.schedule.wake_seconds) ||
      !cJSON_AddBoolToObject(updated.get(), "portrait_pairing", settings.portrait_pairing) ||
      !cJSON_AddStringToObject(updated.get(), "display_mode", display_mode_name(settings.display_mode)) ||
      !cJSON_AddStringToObject(updated.get(), "screen_rotation", screen_rotation_name(settings.screen_rotation)) ||
      !cJSON_AddStringToObject(updated.get(), "photo_source", photo_source_name(settings.photo_source.mode)) ||
      !cJSON_AddStringToObject(updated.get(), "album_id", settings.photo_source.album_id.c_str()) ||
      !cJSON_AddBoolToObject(updated.get(), "show_clock", settings.clock.show) ||
      !cJSON_AddStringToObject(updated.get(), "clock_format", settings.clock.format.c_str()) ||
      !cJSON_AddStringToObject(updated.get(), "timezone", settings.clock.timezone.c_str()))
    return false;
  const std::unique_ptr<char, decltype(&cJSON_free)> serialized(cJSON_PrintUnformatted(updated.get()), cJSON_free);
  return serialized && parse_all(serialized.get(), settings) && write(serialized.get());
}

}  // namespace espframe::native
