#include "cJSON.h"
#include "native_configuration.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "settings.h"

#include <cassert>
#include <cstring>
#include <map>
#include <memory>
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
    return ESP_ERR_NVS_NOT_FOUND;
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
  using espframe::native::ScreenRotation;

  stored = {{"ssid", "Legacy"}, {"password", "legacy secret"}, {"immich_url", "http://legacy.local/"},
            {"immich_key", "legacy-key"}};
  Settings legacy;
  const auto legacy_store = stored;
  assert(legacy.read());
  assert(legacy.immich_url == "http://legacy.local/api" && legacy.slide_seconds == 15);
  assert(legacy.portrait_pairing);
  assert(legacy.brightness_day == 64);
  assert(!legacy.brightness_night && legacy.night_brightness() == 64);
  assert(legacy.photo_source.mode == PhotoSourceMode::Library);
  assert(legacy.display_mode == DisplayMode::Fit);
  assert(legacy.screen_rotation == ScreenRotation::Normal);
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
  assert(current.metadata.show_date && current.metadata.show_location && !current.metadata.relative);
  assert(!current.tone.base_enabled && !current.tone.night_enabled && current.tone.night_amount == 50);
  assert(current.photo_source.memory_window == 2 && current.photo_source.memory_fallback);
  assert(current.screen_rotation == ScreenRotation::Normal);
  assert(!current.schedule.enabled && current.schedule.wake_seconds == 60);
  auto before = stored;
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","slide_seconds":20.3})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","slide_seconds":86401})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://name@photos.test","slide_seconds":20})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","immich_key":"key with space"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","portrait_pairing":"false"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","display_mode":"Stretch"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","display_mode":true})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","screen_rotation":180})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","screen_rotation":"90"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","screen_rotation":"270"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","timezone":"Unverified/Zone"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","show_clock":12})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","clock_format":"Seconds"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","photo_metadata_date_enabled":"false"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","photo_metadata_date_format":"Unknown"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","photo_metadata_date_taken_format":false})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","base_tone":101})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","base_tone_enabled":"true"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","warm_tone_intensity":9})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","warm_tones_enabled":1})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","brightness_day":9})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","brightness_day":100.5})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","brightness_day":"80"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","brightness_night":9})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","brightness_night":100.5})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","brightness_night":null})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","schedule_enabled":"true"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","schedule_on_hour":24})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","schedule_off_hour":6.5})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","schedule_wake_timeout":9})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","schedule_wake_timeout":3601})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","photo_source":"album"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","photo_source":"unknown"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","memories_window":"Within 60 Days"})"));
  assert(!Settings::save_playback(R"({"immich_url":"https://photos.test","memories_fallback":0})"));
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
  assert(current.read() && !current.brightness_night && current.night_brightness() == 42);
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","brightness_night":26})"));
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","screen_rotation":"180"})"));
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","schedule_enabled":true,
                                  "schedule_on_hour":23,"schedule_off_hour":0,"schedule_wake_timeout":3600})"));
  assert(Settings::save_playback(R"json({"immich_url":"http://photos.test","show_clock":true,"clock_format":"12 Hour",
                                       "timezone":"America/New_York (GMT-5)"})json"));
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","photo_metadata_date_enabled":true,
                                  "photo_metadata_location_enabled":false,"photo_metadata_date_format":"Relative Date",
                                  "photo_metadata_date_taken_format":"January 1, 2026"})"));
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","base_tone_enabled":true,"base_tone":32,
                                  "warm_tones_enabled":true,"warm_tone_intensity":65})"));
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","immich_key":"next-key","slide_seconds":2})"));
  assert(current.read());
  assert(current.immich_key == "next-key" && current.slide_seconds == 2);
  assert(current.clock.show && current.clock.format == "12 Hour" && current.clock.timezone == "America/New_York (GMT-5)");
  assert(current.metadata.needs_time() && !current.metadata.show_location && current.metadata.month_before_day);
  assert(current.tone.base_enabled && current.tone.base_amount == 32);
  assert(current.tone.night_enabled && current.tone.night_amount == 65);
  assert(current.brightness_day == 42);
  assert(current.brightness_night == 26 && current.night_brightness() == 26);
  assert(current.schedule.enabled && current.schedule.on_hour == 23 && current.schedule.off_hour == 0 &&
         current.schedule.wake_seconds == 3600);
  assert(!current.portrait_pairing);
  assert(current.display_mode == DisplayMode::Fill);
  assert(current.screen_rotation == ScreenRotation::UpsideDown);
  assert(Settings::save_playback(
      R"({"immich_url":"http://photos.test","photo_source":"album","album_id":"C48E2980-18AC-4D7D-876A-A636759A1AEC"})"));
  assert(current.read());
  assert(current.photo_source.mode == PhotoSourceMode::Album);
  assert(current.photo_source.album_id == "c48e2980-18ac-4d7d-876a-a636759a1aec");
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","photo_source":"memories",
                                  "memories_window":"Within 7 Days","memories_fallback":false})"));
  assert(current.read() && current.photo_source.mode == PhotoSourceMode::Memories);
  assert(current.photo_source.memory_window == 4 && !current.photo_source.memory_fallback);
  assert(current.photo_source.album_id == "c48e2980-18ac-4d7d-876a-a636759a1aec");
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","slide_seconds":86400})"));
  assert(current.read() && current.slide_seconds == 86'400);
  assert(current.immich_key == "next-key" && !current.portrait_pairing);
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","photo_source":"favorites"})"));
  assert(current.read() && current.photo_source.mode == PhotoSourceMode::Favorites);
  assert(current.photo_source.memory_window == 4 && !current.photo_source.memory_fallback);
  assert(current.photo_source.album_id == "c48e2980-18ac-4d7d-876a-a636759a1aec");
  assert(!current.capture_dates.enabled && !current.capture_dates.relative && current.capture_dates.years);
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","date_filter_enabled":true,
    "date_filter_mode":"Relative Range","date_from":"2024-02-29","date_to":"","relative_amount":2,"relative_unit":"Months"})"));
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","photo_source":"memories"})"));
  assert(current.read() && current.capture_dates.needs_time() && current.capture_dates.relative);
  assert(current.capture_dates.from == "2024-02-29" && current.capture_dates.through.empty());
  assert(current.capture_dates.amount == 2 && !current.capture_dates.years);
  assert(!Settings::save_playback(R"({"immich_url":"http://photos.test","date_to":"2024-02-28"})"));
  assert(!Settings::save_playback(R"({"immich_url":"http://photos.test","date_from":"2023-02-29"})"));
  assert(!Settings::save_playback(R"({"immich_url":"http://photos.test","relative_amount":121})"));
  assert(!Settings::save_playback(R"({"immich_url":"http://photos.test","relative_unit":"Weeks"})"));
  assert(current.read() && current.capture_dates.amount == 2 && current.capture_dates.through.empty());
  stored = legacy_store;
  assert(current.read());
  assert(current.web_password.empty() && current.slide_seconds == 15);
  assert(current.portrait_pairing);
  assert(current.photo_source.mode == PhotoSourceMode::Library && current.photo_source.album_id.empty());
  assert(current.display_mode == DisplayMode::Fit);
  assert(!current.capture_dates.enabled && !current.capture_dates.relative && current.capture_dates.years);

  const auto new_wifi = espframe::native::WifiCredentials::from("New Wi-Fi", "New secret");
  assert(new_wifi);
  stored.clear();
  assert(!Settings::save(R"({"immich_url":"http://photos.test","immich_key":"test-key"})"));
  assert(!Settings::save(R"({"ssid":"Lab","password":"","immich_url":"","immich_key":""})"));
  assert(Settings::save_wifi(*new_wifi));
  assert(current.read() && current.ssid == "New Wi-Fi" && current.password == "New secret" && !current.has_immich());
  assert(!Settings::save(R"({"ssid":"Another Wi-Fi","immich_url":"http://photos.test","immich_key":"test-key"})"));
  assert(Settings::save(R"({"immich_url":"http://photos.test","immich_key":"test-key",
                            "slide_seconds":25,"screen_rotation":"180","web_password":"only-settings"})"));
  assert(current.read() && current.ssid == "New Wi-Fi" && current.has_immich());
  const auto replacement_wifi = espframe::native::WifiCredentials::from("Different Wi-Fi", "");
  assert(replacement_wifi && Settings::save_wifi(*replacement_wifi));
  assert(current.read() && current.ssid == "Different Wi-Fi" && current.password.empty());
  assert(current.immich_key == "test-key" && current.slide_seconds == 25 && current.web_password == "only-settings");
  assert(current.screen_rotation == ScreenRotation::UpsideDown);
  stored["settings_v1"] = "malformed";
  assert(!Settings::save_wifi(*new_wifi));
  assert(stored["settings_v1"] == "malformed");

  std::string quoted_password;
  for (int i = 0; i < 64; ++i)
    quoted_password += R"(\")";
  const std::string endpoint = "http://test/" + std::string(239, 'p');
  const std::string large = "{\"ssid\":\"" + std::string(32, 's') + "\",\"password\":\"" + quoted_password +
      "\",\"web_password\":\"" + quoted_password + "\",\"immich_url\":\"" + endpoint +
      "\",\"immich_key\":\"" + std::string(256, 'k') + "\"}";
  assert(Settings::save(large.c_str()));
  const auto playback = "{\"immich_url\":\"" + endpoint + "\",\"brightness_night\":100,\"slide_seconds\":86400," +
      R"("photo_source":"memories","album_id":"c48e2980-18ac-4d7d-876a-a636759a1aec","memories_window":"Within 7 Days"})";
  assert(Settings::save_playback(playback.c_str()));
  assert(stored.at("settings_v1").size() > 1536 && current.read());
  assert(current.web_password == std::string(64, '"') && current.photo_source.memory_window == 4);

  using espframe::native::configuration_capabilities;
  using espframe::native::configuration_snapshot;
  using espframe::native::decode_configuration_form;
  using espframe::native::update_configuration;
  using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
  const Json capabilities(configuration_capabilities(), cJSON_Delete);
  assert(capabilities && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(capabilities.get(), "configuration_write")));
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(capabilities.get(), "legacy_entity_api")));

  const Json snapshot(configuration_snapshot(current), cJSON_Delete);
  assert(snapshot && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(snapshot.get(), "api_key_configured")));
  assert(cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(snapshot.get(), "reset_epoch")) == 0);
  const auto *values = cJSON_GetObjectItemCaseSensitive(snapshot.get(), "values");
  assert(cJSON_IsObject(values));
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(values, "interval")) == std::string("24 hours"));
  assert(!cJSON_HasObjectItem(values, "api_key") && !cJSON_HasObjectItem(values, "immich_key") &&
         !cJSON_HasObjectItem(values, "web_password") && !cJSON_HasObjectItem(values, "password") &&
         !cJSON_HasObjectItem(values, "photo_source"));
  bool source_unavailable = false;
  const cJSON *key;
  cJSON_ArrayForEach(key, cJSON_GetObjectItemCaseSensitive(snapshot.get(), "unavailable")) {
    source_unavailable |= cJSON_GetStringValue(key) == std::string("photo_source");
  }
  assert(source_unavailable);

  const auto form = decode_configuration_form(
      "configuration=%7B%22values%22%3A%7B%22interval%22%3A%2230+seconds%22%2C%22api_key%22%3A%22new%2B%26key%22%7D%7D");
  assert(form && *form == R"({"values":{"interval":"30 seconds","api_key":"new+&key"}})");
  assert(!decode_configuration_form("configuration=%00"));
  assert(!decode_configuration_form("configuration=%0Q"));
  assert(!decode_configuration_form("configuration=%"));
  assert(!decode_configuration_form("configuration=%7B&configuration=%7D"));
  assert(!decode_configuration_form("configuration=" + std::string(Settings::max_json_size + 1, 'x')));
  const auto response = update_configuration(form->c_str());
  assert(!response.error && response.updated == 2);
  assert(current.read() && current.slide_seconds == 30 && current.immich_key == "new+&key");
  assert(current.photo_source.mode == PhotoSourceMode::Memories && current.immich_url == endpoint + "/api");
  assert(current.web_password == std::string(64, '"') && current.password == std::string(64, '"'));

  const auto saved = stored;
  for (const auto *invalid : {
           R"({"api_version":2,"values":{"brightness_day":50}})",
           R"({"reset_epoch":1,"values":{"brightness_day":50}})",
           R"({"values":{"brightness_day":50,"photo_source":"Custom"}})",
           R"({"values":{"brightness_day":50,"screen_rotation":"90"}})",
           R"({"values":{"brightness_day":50,"api_key":""}})",
           R"({"values":{"brightness_day":50,"brightness_day":80}})",
           R"({"values":{"brightness_day":50,"unsupported":true}})",
           R"({"values":{"brightness_day":50,"api_key":"partial\u0000key"}})",
           R"({"values":{"brightness_day":50,"api_key":[[[["key"]]]]}})",
       }) {
    assert(update_configuration(invalid).error && stored == saved);
  }
  assert(!update_configuration(R"({"values":{}})").error && stored == saved);
  assert(!Settings::save_playback(R"({"brightness_day":60})"));
  assert(Settings::save_playback(R"({"immich_url":"https://photos.test","brightness_day":60})"));
  assert(current.read() && current.brightness_day == 60 && current.immich_key == "new+&key");
}
