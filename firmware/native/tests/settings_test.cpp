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
  using espframe::native::DisplayMode;
  using espframe::native::PhotoSourceMode;

  stored = {{"ssid", "Legacy"}, {"password", "legacy secret"}, {"immich_url", "http://legacy.local/"},
            {"immich_key", "legacy-key"}};
  Settings legacy;
  const auto legacy_store = stored;
  assert(legacy.read());
  assert(legacy.immich_url == "http://legacy.local/api" && legacy.slide_seconds == 15);
  assert(legacy.portrait_pairing);
  assert(legacy.brightness_day == 64);
  assert(legacy.photo_source.mode == PhotoSourceMode::Library);
  assert(legacy.display_mode == DisplayMode::Fit);
  assert(!legacy.clock.show && legacy.clock.format == "24 Hour");
  assert(!legacy.schedule.enabled && legacy.schedule.on_hour == 6 && legacy.schedule.off_hour == 23 &&
         legacy.schedule.wake_seconds == 60);
  assert(legacy.web_password.empty());
  assert(!Settings::save_playback(R"({"immich_url":"http://new.local"})"));

  assert(Settings::save(R"({"ssid":"Photo Lab","password":"wifi secret",
                           "immich_url":"https://photos.test/api/","immich_key":"first-key",
                           "web_password":"only-settings","slide_seconds":20})"));
  Settings current;
  assert(current.read());
  assert(current.immich_url == "https://photos.test/api" && current.slide_seconds == 20);
  assert(current.web_password == "only-settings");
  assert(current.display_mode == DisplayMode::Fit);
  assert(!current.schedule.enabled && current.schedule.wake_seconds == 60);
  auto before = stored;
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","slide_seconds":20.3})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","slide_seconds":86401})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://name@photos.test","slide_seconds":20})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","immich_key":"key with space"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","portrait_pairing":"false"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","display_mode":"Stretch"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","display_mode":true})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","timezone":"Unverified/Zone"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","show_clock":12})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","clock_format":"Seconds"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","brightness_day":9})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","brightness_day":100.5})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","brightness_day":"80"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","schedule_enabled":"true"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","schedule_on_hour":24})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","schedule_off_hour":6.5})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","schedule_wake_timeout":9})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","schedule_wake_timeout":3601})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","photo_source":"album"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","photo_source":"unknown"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","album_id":"not-an-album"})"));
  assert(stored == before);

  assert(Settings::save_playback(
      R"({"immich_url":"https://photos.test/family","immich_key":"","slide_seconds":35,"portrait_pairing":false,"display_mode":"Fill"})"));
  assert(current.read());
  assert(current.immich_url == "https://photos.test/family/api" && current.slide_seconds == 35);
  assert(!current.portrait_pairing);
  assert(current.display_mode == DisplayMode::Fill);
  assert(current.ssid == "Photo Lab" && current.password == "wifi secret");
  assert(current.web_password == "only-settings" && current.immich_key == "first-key");
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","brightness_day":42})"));
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","schedule_enabled":true,
                                  "schedule_on_hour":23,"schedule_off_hour":0,"schedule_wake_timeout":3600})"));
  assert(Settings::save_playback(R"json({"immich_url":"http://photos.test","show_clock":true,"clock_format":"12 Hour",
                                       "timezone":"America/New_York (GMT-5)"})json"));
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","immich_key":"next-key","slide_seconds":2})"));
  assert(current.read());
  assert(current.immich_key == "next-key" && current.slide_seconds == 2);
  assert(current.clock.show && current.clock.format == "12 Hour" && current.clock.timezone == "America/New_York (GMT-5)");
  assert(current.brightness_day == 42);
  assert(current.schedule.enabled && current.schedule.on_hour == 23 && current.schedule.off_hour == 0 &&
         current.schedule.wake_seconds == 3600);
  assert(!current.portrait_pairing);
  assert(current.display_mode == DisplayMode::Fill);
  assert(Settings::save_playback(
      R"({"immich_url":"http://photos.test","photo_source":"album","album_id":"C48E2980-18AC-4D7D-876A-A636759A1AEC"})"));
  assert(current.read());
  assert(current.photo_source.mode == PhotoSourceMode::Album);
  assert(current.photo_source.album_id == "c48e2980-18ac-4d7d-876a-a636759a1aec");
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","slide_seconds":86400})"));
  assert(current.read() && current.slide_seconds == 86'400);
  assert(current.immich_key == "next-key" && !current.portrait_pairing);
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","photo_source":"favorites"})"));
  assert(current.read() && current.photo_source.mode == PhotoSourceMode::Favorites);
  assert(current.photo_source.album_id == "c48e2980-18ac-4d7d-876a-a636759a1aec");
  stored = legacy_store;
  assert(current.read());
  assert(current.web_password.empty() && current.slide_seconds == 15);
  assert(current.portrait_pairing);
  assert(current.photo_source.mode == PhotoSourceMode::Library && current.photo_source.album_id.empty());
  assert(current.display_mode == DisplayMode::Fit);
}
