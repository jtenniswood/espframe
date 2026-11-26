#include "cJSON.h"
#include "frame_identity.h"
#include "frame_status.h"
#include "home_assistant_key.h"
#include "i18n.h"
#include "initial_photo_timeout.h"
#include "native_configuration.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "playback_transition.h"
#include "settings.h"
#include "slideshow_interval.h"
#include "stock_connection_preferences.h"
#include "stock_settings.h"

#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace {

std::map<std::string, std::string> stored;
std::map<std::string, std::optional<std::string>> staged;
std::set<std::string> stored_blobs;
std::set<std::string> staged_blobs;
bool fail_commit{};
bool fail_erase{};
constexpr nvs_handle_t stock_handle = 2;
constexpr nvs_handle_t stock_identity_handle = 3;
bool stock_partition_available{true};
bool stock_initialized{};
bool stock_opened{};
std::map<std::string, std::string> stock_preferences;
unsigned stock_reads{};

}  // namespace

esp_err_t nvs_flash_init_partition(const char *partition) {
  if (std::string_view(partition) == "stock_prefs") {
    assert(!stock_initialized);
    if (!stock_partition_available)
      return ESP_ERR_NVS_NOT_FOUND;
    stock_initialized = true;
    return ESP_OK;
  }
  assert(std::string(partition) == "native_config");
  return ESP_OK;
}

esp_err_t nvs_flash_deinit_partition(const char *partition) {
  assert(std::string_view(partition) == "stock_prefs" && stock_initialized && !stock_opened);
  stock_initialized = false;
  return ESP_OK;
}

esp_err_t nvs_open_from_partition(const char *partition, const char *name, int mode, nvs_handle_t *handle) {
  if (std::string_view(partition) == "stock_prefs") {
    const std::string_view space(name);
    assert((space == "esphome" || space == "espframe_id") && mode == NVS_READONLY && stock_initialized && !stock_opened);
    stock_opened = true;
    *handle = space == "espframe_id" ? stock_identity_handle : stock_handle;
    return ESP_OK;
  }
  assert(std::string(partition) == "native_config" && std::string(name) == "player");
  *handle = mode;
  return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t, const char *key, char *out, size_t *size) {
  auto value = stored.find(key);
  if (value == stored.end())
    return ESP_ERR_NVS_NOT_FOUND;
  if (stored_blobs.contains(key))
    return ESP_ERR_NVS_TYPE_MISMATCH;
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
  staged_blobs.erase(key);
  return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size) {
  if (handle == stock_handle || handle == stock_identity_handle) {
    assert(stock_opened && out);
    ++stock_reads;
    const auto value = stock_preferences.find(handle == stock_identity_handle ? std::string("espframe_id/") + key : key);
    if (value == stock_preferences.end())
      return ESP_ERR_NVS_NOT_FOUND;
    if (*size < value->second.size())
      return ESP_ERR_NVS_TYPE_MISMATCH;
    *size = value->second.size();
    std::memcpy(out, value->second.data(), *size);
    return ESP_OK;
  }
  const auto value = stored.find(key);
  if (value == stored.end())
    return ESP_ERR_NVS_NOT_FOUND;
  if (!stored_blobs.contains(key))
    return ESP_ERR_NVS_TYPE_MISMATCH;
  if (out != nullptr) {
    assert(*size >= value->second.size());
    std::memcpy(out, value->second.data(), value->second.size());
  }
  *size = value->second.size();
  return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size) {
  assert(handle == NVS_READWRITE);
  staged[key] = std::string(static_cast<const char *>(value), size);
  staged_blobs.insert(key);
  return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
  assert(handle == NVS_READWRITE);
  if (fail_erase)
    return -1;
  if (!stored.contains(key))
    return ESP_ERR_NVS_NOT_FOUND;
  staged[key] = std::nullopt;
  return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
  assert(handle == NVS_READWRITE);
  if (fail_commit)
    return -1;
  for (const auto &[key, value] : staged) {
    stored_blobs.erase(key);
    if (value)
      stored[key] = *value;
    else
      stored.erase(key);
    if (staged_blobs.contains(key))
      stored_blobs.insert(key);
  }
  return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
  if (handle == stock_handle || handle == stock_identity_handle) {
    assert(stock_opened);
    stock_opened = false;
    return;
  }
  staged.clear();
  staged_blobs.clear();
}

void verify_setup_framing() {
  using namespace espframe::native;
  constexpr char first_setup[] = R"({"ssid":"Photo Lab","password":"","immich_url":"http://photos.test",
                                    "immich_key":"first-key"})";
  constexpr char reuse_wifi[] = R"({"immich_url":"http://photos.test","immich_key":"next-key"})";
  stored.clear();
  stored_blobs.clear();
  Settings settings;
  assert(Settings::save(first_setup));
  assert(settings.read() && settings.display_mode == DisplayMode::Fill && settings.pair_display_mode() == DisplayMode::Fill);

  assert(Settings::save(R"({"immich_url":"http://photos.test","immich_key":"next-key",
                            "display_mode":"Fit","portrait_pair_framing":"Fill"})"));
  assert(Settings::save(reuse_wifi));
  assert(settings.read() && settings.display_mode == DisplayMode::Fit && settings.pair_display_mode() == DisplayMode::Fill);

  stored_blobs.clear();
  stored = {{"settings_v1", R"({"ssid":"Photo Lab","password":"","immich_url":"http://photos.test",
                                "immich_key":"legacy-key"})"}};
  assert(Settings::save(reuse_wifi));
  assert(settings.read() && settings.display_mode == DisplayMode::Fit);

  stored.clear();
  stored_blobs.clear();
  const auto wifi = WifiCredentials::from("Photo Lab", "");
  assert(wifi && Settings::save_wifi(*wifi, 0));
  assert(settings.read() && !settings.has_immich() && settings.display_mode == DisplayMode::Fill);
  assert(Settings::save(reuse_wifi));
  assert(settings.read() && settings.display_mode == DisplayMode::Fill);

  stored_blobs.clear();
  stored = {{"settings_v1", R"({"ssid":"Photo Lab","password":"","immich_url":"","immich_key":"",
                                "display_mode":"Fit"})"}};
  assert(Settings::save(reuse_wifi));
  assert(settings.read() && settings.display_mode == DisplayMode::Fit);
}

void verify_stock_pairing_migration() {
  using namespace espframe::native;
  using Result = Settings::UsbSaveResult;
  constexpr char setup[] = R"({"immich_url":"http://photos.test","immich_key":"fresh-key","web_password":"new-settings"})";
  constexpr char manual[] = R"({"immich_url":"http://photos.test","immich_key":"fresh-key","web_password":"new-settings",
      "home_assistant_key":""})";
  constexpr char expected_key[] = "AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA=";
  const auto key = decode_home_assistant_key(expected_key);
  assert(key && encode_home_assistant_key(*key) == expected_key);

  stored.clear();
  stored_blobs.clear();
  stock_preferences.clear();
  const auto wifi = WifiCredentials::from("Photo Lab", "test-password");
  assert(wifi && Settings::save_wifi(*wifi, 0));
  const auto before = stored;
  constexpr StockSettingsRequest pairing{.pairing = true};
  const auto migrate = [&] { return Settings::save_usb(setup, 0, pairing, read_stock_settings); };
  stock_partition_available = false;
  assert(migrate() == Result::PairingUnavailable && stored == before);
  stock_partition_available = true;
  assert(migrate() == Result::PairingUnavailable && stored == before);
  for (size_t bytes : {31, 32, 33}) {
    stock_preferences["88491486"] = std::string(bytes, '\0');
    assert(migrate() == Result::PairingUnavailable && stored == before);
    assert(!stock_initialized && !stock_opened);
  }

  stock_preferences["88491486"] = std::string(reinterpret_cast<const char *>(key->data()), key->size());
  std::string stock_name(128, '\0');
  stock_name[0] = 1;
  stock_name.replace(4, std::strlen("  Salon été  "), "  Salon été  ");
  stock_preferences["espframe_id/identity"] = stock_name;
  const auto original = stock_preferences;
  const auto initial_reads = stock_reads;
  assert(Settings::save_usb(manual, 0, pairing, read_stock_settings) == Result::Rejected);
  assert(Settings::save_usb(setup, 1, pairing, read_stock_settings) == Result::Rejected);
  assert(stock_reads == initial_reads && stored == before);

  fail_commit = true;
  assert(migrate() == Result::Rejected && stored == before);
  fail_commit = false;
  assert(migrate() == Result::Saved);
  Settings loaded;
  assert(loaded.read() && loaded.home_assistant_key == expected_key && loaded.ssid == "Photo Lab");
  assert(loaded.frame_name == "Salon été");
  constexpr FrameMac mac{0x02, 0xab, 0xcd, 0xef, 0x12, 0x9a};
  assert(make_frame_identity(loaded.frame_name, mac, "immich-frame-10inch", "Espframe 10inch", true).hostname ==
         "salon-t-129a");
  const auto imported_reads = stock_reads;
  assert(migrate() == Result::PairingUnavailable && stock_reads == imported_reads);
  assert(Settings::save(setup, 0) && loaded.read() && loaded.home_assistant_key == expected_key &&
         loaded.frame_name == "Salon été");
  assert(Settings::save(manual, 0) && loaded.read() && loaded.home_assistant_key.empty());
  assert(!Settings::save_slide_interval(45, expected_key));
  assert(Settings::save_playback(R"({"immich_url":"http://photos.test","slide_seconds":45})"));
  assert(migrate() == Result::PairingUnavailable && stock_reads == imported_reads);
  assert(Settings::reset(ResetMode::Factory));
  assert(Settings::save_wifi(*wifi, 1));
  assert(Settings::save_usb(setup, 1, pairing, read_stock_settings) == Result::PairingUnavailable);
  assert(loaded.read() && loaded.home_assistant_key.empty() && stock_preferences == original);
  assert(stock_reads == imported_reads && !stock_initialized && !stock_opened);
  stored["settings_v1"] = "{corrupt";
  assert(migrate() == Result::PairingUnavailable && stock_reads == imported_reads);

  stored.clear();
  stored_blobs.clear();
  assert(Settings::save_wifi(*wifi, 0));
  constexpr char explicit_name[] = R"({"immich_url":"http://photos.test","immich_key":"fresh-key",
                                      "frame_name":"Backup name"})";
  assert(Settings::save_usb(explicit_name, 0, pairing, read_stock_settings) == Result::Saved);
  assert(loaded.read() && loaded.frame_name == "Backup name");

  stored.clear();
  stored_blobs.clear();
  assert(Settings::save_wifi(*wifi, 0) && Settings::save(explicit_name, 0));
  assert(migrate() == Result::Saved && loaded.read() && loaded.frame_name == "Backup name");

  stored.clear();
  stored_blobs.clear();
  assert(Settings::save_wifi(*wifi, 0));
  assert(Settings::save(R"({"immich_url":"http://photos.test","immich_key":"fresh-key","frame_name":""})"));
  assert(Settings::save(setup, 0));
  assert(migrate() == Result::Saved && loaded.read() && loaded.frame_name.empty() && stock_preferences == original);
}

void verify_stock_connection_migration() {
  using namespace espframe::native;
  using Result = Settings::UsbSaveResult;
  constexpr StockSettingsRequest both{.wifi = true, .immich = true};
  constexpr StockSettingsRequest all{.wifi = true, .immich = true, .pairing = true};
  constexpr StockSettingsRequest wifi_only{.wifi = true};
  constexpr StockSettingsRequest immich_only{.immich = true};
  constexpr char setup[] = R"({"slide_seconds":35,"display_mode":"Fill","web_password":"frame-web-test"})";
  constexpr char manual_immich[] = R"({"immich_url":"https://replacement.test","immich_key":"replacement-key"})";

  stored.clear();
  stored_blobs.clear();
  stock_preferences.clear();
  assert(Settings::save_usb(setup, 0, both, read_stock_settings) == Result::WifiUnavailable);
  assert(stored.empty());
  const auto station = WifiCredentials::from("Photo Lab", "fixture-password");
  assert(station);
  const auto wifi = std::string(station->ssid.data(), station->ssid.size()) +
                    std::string(station->password.data(), station->password.size());
  const auto text_record = [](std::string_view value, size_t size) {
    std::string record(size, '\0');
    record[0] = static_cast<char>(value.size());
    record.replace(1, value.size(), value);
    return record;
  };
  stock_preferences[esphome_station_key] = wifi.substr(0, wifi.size() - 1);
  assert(Settings::save_usb(setup, 0, both, read_stock_settings) == Result::WifiUnavailable && stored.empty());
  stock_preferences[esphome_station_key] = wifi;
  stock_preferences[esphome_immich_url_key] = text_record("https://stock.test/library/", esphome_url_bytes);
  assert(Settings::save_usb(setup, 0, both, read_stock_settings) == Result::ImmichUnavailable && stored.empty());
  stock_preferences[esphome_immich_api_key] = text_record("bad key", esphome_api_key_bytes);
  assert(Settings::save_usb(setup, 0, both, read_stock_settings) == Result::ImmichUnavailable && stored.empty());
  stock_preferences[esphome_immich_api_key] = text_record("fixture-immich-key", esphome_api_key_bytes);
  assert(Settings::save_usb(setup, 0, all, read_stock_settings) == Result::PairingUnavailable && stored.empty());
  stock_preferences["88491486"] = std::string(32, '\x01');
  const auto source = stock_preferences;
  const auto before_conflicts = stock_reads;
  assert(Settings::save_usb(manual_immich, 0, both, read_stock_settings) == Result::Rejected);
  assert(Settings::save_usb(R"({"ssid":"conflict"})", 0, both, read_stock_settings) == Result::Rejected);
  assert(stock_reads == before_conflicts && stored.empty());
  fail_commit = true;
  assert(Settings::save_usb(setup, 0, all, read_stock_settings) == Result::Rejected && stored.empty());
  fail_commit = false;
  assert(Settings::save_usb(setup, 0, all, read_stock_settings) == Result::Saved);
  Settings loaded;
  assert(loaded.read() && loaded.ssid == "Photo Lab" && loaded.password == "fixture-password");
  assert(loaded.immich_url == "https://stock.test/library/api" && loaded.immich_key == "fixture-immich-key");
  const auto pairing = decode_home_assistant_key(loaded.home_assistant_key);
  assert(pairing && pairing->front() == 1 && pairing->back() == 1);
  assert(loaded.slide_seconds == 35 && loaded.display_mode == DisplayMode::Fill);
  const auto after_import = stock_reads;
  assert(Settings::save_usb(setup, 0, both, read_stock_settings) == Result::WifiUnavailable);
  assert(Settings::save_usb(setup, 0, immich_only, read_stock_settings) == Result::ImmichUnavailable);
  assert(Settings::reset(ResetMode::Factory));
  assert(Settings::save_usb(setup, 1, both, read_stock_settings) == Result::WifiUnavailable);
  assert(Settings::save_usb(setup, 1, immich_only, read_stock_settings) == Result::ImmichUnavailable);
  assert(stock_reads == after_import && stock_preferences == source && !stock_initialized && !stock_opened);

  stored.clear();
  stored_blobs.clear();
  assert(Settings::save_usb(manual_immich, 0, wifi_only, read_stock_settings) == Result::Saved);
  assert(stock_reads == after_import + 1);
  assert(loaded.read() && loaded.ssid == "Photo Lab" && loaded.immich_url == "https://replacement.test/api");
  stored.clear();
  stored_blobs.clear();
  const auto native_wifi = WifiCredentials::from("Native Network", "");
  assert(native_wifi && Settings::save_wifi(*native_wifi, 0));
  assert(Settings::save_usb(setup, 0, immich_only, read_stock_settings) == Result::Saved);
  assert(stock_reads == after_import + 3);
  assert(loaded.read() && loaded.ssid == "Native Network" && loaded.immich_url == "https://stock.test/library/api");
  assert(stock_preferences == source);

  stored.clear();
  stored_blobs.clear();
  stock_preferences[esphome_immich_url_key] = text_record("http://api", esphome_url_bytes);
  assert(Settings::save_wifi(*native_wifi, 0));
  assert(Settings::save_usb(setup, 0, immich_only, read_stock_settings) == Result::Saved);
  assert(loaded.read() && loaded.immich_url == "http://api/api" && loaded.immich_key == "fixture-immich-key");
}

int main() {
  using espframe::native::Settings;
  using espframe::native::DisplayMode;
  using espframe::native::PhotoSourceMode;
  using espframe::native::ScreenRotation;
  using espframe::native::active_slideshow_interval;

  stored = {{"ssid", "Legacy"}, {"password", "legacy secret"}, {"immich_url", "http://legacy.local/"},
            {"immich_key", "legacy-key"}};
  Settings legacy;
  const auto legacy_store = stored;
  assert(legacy.read());
  assert(legacy.immich_url == "http://legacy.local/api" && legacy.slide_seconds == 15);
  assert(legacy.connection_timeout_seconds == 600);
  assert(legacy.transition == "Cross Fade" && legacy.transition_speed == "Fast");
  assert(legacy.portrait_pairing);
  assert(!legacy.portrait_pairs_only && !legacy.requires_portrait_pair() && legacy.portrait_pairing_days() == 0);
  assert(legacy.brightness_day == 64);
  assert(!legacy.tone.night_override);
  assert(legacy.metadata.language == 0);
  assert(!legacy.brightness_night && legacy.night_brightness() == 64);
  assert(legacy.photo_source.mode == PhotoSourceMode::Library);
  assert(legacy.display_mode == DisplayMode::Fit);
  assert(legacy.photo_orientation == espframe::native::PhotoOrientation::Any);
  assert(legacy.screen_rotation == ScreenRotation::Normal);
  assert(!legacy.developer_features_enabled);
  assert(!legacy.clock.show && legacy.clock.format == "24 Hour");
  assert((legacy.clock.ntp_servers == std::array<std::string, 3>{"pool.ntp.org", "", ""}));
  assert(!legacy.schedule.enabled && legacy.schedule.on_hour == 6 && legacy.schedule.off_hour == 23 &&
         legacy.schedule.wake_seconds == 60);
  assert(legacy.web_password.empty());
  assert(Settings::save_playback(R"({"immich_url":"http://new.local"})"));
  assert(legacy.read() && legacy.immich_url == "http://new.local/api");

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
  assert(Settings::save_wifi(*new_wifi, 0));
  assert(current.read() && current.ssid == "New Wi-Fi" && current.password == "New secret" && !current.has_immich());
  assert(!Settings::save(R"({"ssid":"Another Wi-Fi","immich_url":"http://photos.test","immich_key":"test-key"})"));
  assert(Settings::save(R"({"immich_url":"http://photos.test","immich_key":"test-key",
                            "slide_seconds":25,"screen_rotation":"180","web_password":"only-settings"})"));
  assert(current.read() && current.ssid == "New Wi-Fi" && current.has_immich());
  const auto replacement_wifi = espframe::native::WifiCredentials::from("Different Wi-Fi", "");
  assert(replacement_wifi && Settings::save_wifi(*replacement_wifi, 0));
  assert(current.read() && current.ssid == "Different Wi-Fi" && current.password.empty());
  assert(current.immich_key == "test-key" && current.slide_seconds == 25 && current.web_password == "only-settings");
  assert(current.screen_rotation == ScreenRotation::UpsideDown);
  stored["settings_v1"] = "malformed";
  assert(!Settings::save_wifi(*new_wifi, 0));
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
  using espframe::native::decode_reset_form;
  using espframe::native::reset_configuration;
  using espframe::native::ResetMode;
  using espframe::native::update_configuration;
  using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
  const Json capabilities(configuration_capabilities(), cJSON_Delete);
  assert(capabilities && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(capabilities.get(), "configuration_write")));
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(capabilities.get(), "legacy_entity_api")));
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(capabilities.get(), "reset_path")) ==
         std::string_view("/espframe/api/v1/reset"));
  const auto *reset_modes = cJSON_GetObjectItemCaseSensitive(capabilities.get(), "reset_modes");
  assert(cJSON_GetArraySize(reset_modes) == 2);
  assert(cJSON_GetStringValue(cJSON_GetArrayItem(reset_modes, 0)) == std::string_view("customization"));
  assert(cJSON_GetStringValue(cJSON_GetArrayItem(reset_modes, 1)) == std::string_view("factory"));

  const Json snapshot(configuration_snapshot(current), cJSON_Delete);
  assert(snapshot && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(snapshot.get(), "api_key_configured")));
  assert(cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(snapshot.get(), "reset_epoch")) == 0);
  const auto *values = cJSON_GetObjectItemCaseSensitive(snapshot.get(), "values");
  assert(cJSON_IsObject(values));
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(values, "interval")) == std::string("24 hours"));
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(values, "transition")) == std::string("Cross Fade"));
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(values, "transition_speed")) == std::string("Fast"));
  assert(!cJSON_HasObjectItem(values, "api_key") && !cJSON_HasObjectItem(values, "immich_key") &&
         !cJSON_HasObjectItem(values, "web_password") && !cJSON_HasObjectItem(values, "password"));
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(values, "photo_source")) == std::string_view("Memories"));
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(values, "immich_filter_support")) ==
         std::string_view("unknown"));

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
           R"({"values":{"brightness_day":50,"photo_source":"custom"}})",
           R"({"values":{"brightness_day":50,"photo_source":"Memories","photo_source":"Custom"}})",
           R"({"values":{"brightness_day":50,"album_labels":true}})",
           R"({"values":{"brightness_day":50,"screen_rotation":"90"}})",
           R"({"values":{"brightness_day":50,"portrait_pairs_only":"true"}})",
           R"({"values":{"brightness_day":50,"transition":"Dissolve"}})",
           R"({"values":{"brightness_day":50,"transition_speed":500}})",
           R"({"values":{"brightness_day":50,"portrait_pairing_range":"Within 4 Days"}})",
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
  const auto transition_update = update_configuration(R"({"values":{"transition":"Slide Down","transition_speed":"Slow"}})");
  assert(!transition_update.error && transition_update.updated == 2);
  assert(current.read() && current.transition == "Slide Down" && current.transition_speed == "Slow");

  assert(!update_configuration(
      R"({"values":{"portrait_pairing":false,"portrait_pairs_only":true,"portrait_pairing_range":"Within 2 Days"}})").error);
  assert(current.read() && !current.portrait_pairing && current.portrait_pairs_only && !current.requires_portrait_pair());
  assert(current.portrait_pairing_days() == 2);
  const Json paused(configuration_snapshot(current), cJSON_Delete);
  assert(paused);
  const auto *paused_values = cJSON_GetObjectItemCaseSensitive(paused.get(), "values");
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(paused_values, "portrait_pairs_only")));
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(paused_values, "portrait_pairing")));
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(paused_values, "portrait_pairing_range")) ==
         std::string_view("Within 2 Days"));
  assert(Settings::save_playback(R"({"immich_url":"https://photos.test","portrait_pairing":true})"));
  assert(current.read() && current.requires_portrait_pair() && current.portrait_pairing_days() == 2);
  assert(Settings::save_playback(R"({"immich_url":"https://photos.test","portrait_pairs_only":false})"));
  assert(current.read() && current.portrait_pairing && !current.requires_portrait_pair());

  for (const auto &[range, days] : std::array<std::pair<std::string_view, int>, 6>{{
           {"Within 3 Days", 3}, {"Within 5 Days", 5}, {"Within 7 Days", 7}, {"Within 14 Days", 14},
           {"Within 30 Days", 30}, {"Any Time", espframe::native::portrait_pairing_any_time}}}) {
    const auto update = R"({"values":{"portrait_pairing_range":")" + std::string(range) + R"("}})";
    assert(!update_configuration(update.c_str()).error);
    assert(current.read() && current.portrait_pairing_days() == days);
    const Json snapshot(configuration_snapshot(current), cJSON_Delete);
    const auto *values = cJSON_GetObjectItemCaseSensitive(snapshot.get(), "values");
    assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(values, "portrait_pairing_range")) == range);
  }

  assert(!current.has_prepared_slides());
  const auto before_service = stored;
  assert(update_configuration(R"({"values":{"prepared_slide_url":"https://prepare.test"}})").error);
  assert(update_configuration(R"({"values":{"prepared_slide_url":"https://prepare.test","prepared_slide_token":"short"}})").error);
  assert(update_configuration(R"({"values":{"prepared_slide_url":"https://key@prepare.test","prepared_slide_token":"0123456789abcdef0123456789abcdef"}})").error);
  assert(stored == before_service);
  assert(!update_configuration(
      R"({"values":{"prepared_slide_url":"https://prepare.test/frame/","prepared_slide_token":"0123456789abcdef0123456789abcdef"}})").error);
  assert(current.read() && current.has_prepared_slides() && current.prepared_slide_url == "https://prepare.test/frame");
  assert(current.prepared_slide_token == "0123456789abcdef0123456789abcdef");
  const Json prepared(configuration_snapshot(current), cJSON_Delete);
  const auto *prepared_values = cJSON_GetObjectItemCaseSensitive(prepared.get(), "values");
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(prepared_values, "prepared_slide_token_configured")));
  assert(!cJSON_HasObjectItem(prepared_values, "prepared_slide_token"));
  assert(!cJSON_HasObjectItem(prepared.get(), "prepared_slide_token"));
  const auto configured_service = stored;
  assert(update_configuration(R"({"values":{"prepared_slide_url":"https://other.test"}})").error);
  assert(update_configuration(R"({"values":{"prepared_slide_token_configured":false}})").error);
  assert(update_configuration(R"({"values":{"prepared_slide_token":"new token 0123456789abcdef0123456789abcdef"}})").error);
  assert(stored == configured_service);
  assert(!update_configuration(R"({"values":{"prepared_slide_url":"https://prepare.test/frame/","brightness_day":58}})").error);
  assert(current.read() && current.has_prepared_slides() && current.brightness_day == 58);
  assert(!update_configuration(R"({"values":{"prepared_slide_url":""}})").error);
  assert(current.read() && !current.has_prepared_slides() && current.prepared_slide_token.empty());

  assert(!update_configuration(R"({"values":{"screen_rotation":"180","date_filter_enabled":true,
      "prepared_slide_url":"https://prepare.test","prepared_slide_token":"0123456789abcdef0123456789abcdef"}})").error);
  const auto before_reset = stored;
  assert(decode_reset_form("mode=customization") == ResetMode::Customization);
  assert(decode_reset_form("mode=factory") == ResetMode::Factory);
  assert(!decode_reset_form("mode=customization&mode=factory"));
  assert(!decode_reset_form("mode=full"));
  fail_commit = true;
  assert(reset_configuration(ResetMode::Customization).error && stored == before_reset);
  fail_commit = false;
  assert(!reset_configuration(ResetMode::Customization).error);
  assert(current.read() && current.reset_epoch == 1 && current.ssid == std::string(32, 's'));
  assert(current.password == std::string(64, '"') && current.web_password == std::string(64, '"'));
  assert(current.immich_url.empty() && current.immich_key.empty() && current.prepared_slide_url.empty() &&
         current.prepared_slide_token.empty());
  assert(current.slide_seconds == 15 && current.brightness_day == 64 && !current.brightness_night &&
         current.screen_rotation == ScreenRotation::Normal && !current.capture_dates.enabled &&
         current.photo_source.mode == PhotoSourceMode::Library);
  assert(current.transition == "Cross Fade" && current.transition_speed == "Fast");
  const Json reset_snapshot(configuration_snapshot(current), cJSON_Delete);
  assert(cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(reset_snapshot.get(), "reset_epoch")) == 1);
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(reset_snapshot.get(), "api_key_configured")));
  const auto after_reset = stored;
  const auto stale = update_configuration(R"({"reset_epoch":0,"values":{"brightness_day":83}})");
  assert(std::string_view(stale.status) == "409 Conflict" && stored == after_reset);
  assert(!Settings::save_playback(R"({"reset_epoch":0,"immich_url":"http://stale.test","immich_key":"stale-key"})"));
  const auto restored_draft = update_configuration(R"({"reset_epoch":1,"values":{
      "immich_url":"http://photos.test","brightness_day":83,"portrait_pair_framing":"Fill"}})");
  assert(!restored_draft.error && restored_draft.restart_required);
  assert(current.read() && !current.has_immich() && current.immich_url == "http://photos.test/api");
  const Json incomplete(configuration_snapshot(current), cJSON_Delete);
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(incomplete.get(), "api_key_configured")));
  assert(update_configuration(R"({"reset_epoch":1,"values":{
      "prepared_slide_url":"https://prepare.test","prepared_slide_token":"0123456789abcdef0123456789abcdef"}})").error);
  assert(!update_configuration(R"({"reset_epoch":1,"values":{"api_key":"fresh-key"}})").error);
  assert(current.read() && current.has_immich() && current.immich_key == "fresh-key" && current.reset_epoch == 1);
  assert(current.brightness_day == 83 && current.pair_framing == espframe::native::PairFraming::Fill);
  assert(Settings::save_wifi(*new_wifi, 1));
  assert(Settings::save(R"({"immich_url":"http://photos.test","immich_key":"reprovisioned","reset_epoch":0,"reset_epoch":2})"));
  assert(current.read() && current.immich_key == "reprovisioned" && current.reset_epoch == 1);

  // A migration can leave the legacy keys alongside the current record.
  stored.insert(legacy_store.begin(), legacy_store.end());
  assert(Settings::save(R"({"immich_url":"http://photos.test","immich_key":"reprovisioned",
      "web_password":"set-again","prepared_slide_url":"https://prepare.test",
      "prepared_slide_token":"0123456789abcdef0123456789abcdef","screen_rotation":"180"})"));
  assert(current.read() && current.prepared_slides_need_time() && current.immich_url == "http://photos.test/api");
  const auto before_incomplete_service = stored;
  assert(!Settings::save(R"({"immich_url":"http://photos.test","immich_key":"reprovisioned",
      "prepared_slide_url":"https://new-prepare.test"})"));
  assert(stored == before_incomplete_service);
  assert(Settings::save(R"({"immich_url":"http://photos.test/api/","immich_key":"reprovisioned"})"));
  assert(current.read() && current.prepared_slide_url == "https://prepare.test" &&
         current.prepared_slide_token == "0123456789abcdef0123456789abcdef");
  assert(Settings::save(R"({"immich_url":"http://other.test","immich_key":"reprovisioned"})"));
  assert(current.read() && !current.has_prepared_slides());
  assert(Settings::save(R"({"immich_url":"http://photos.test","immich_key":"reprovisioned",
      "web_password":"set-again","prepared_slide_url":"http://prepare.test",
      "prepared_slide_token":"0123456789abcdef0123456789abcdef","screen_rotation":"180"})"));
  assert(current.read() && current.has_prepared_slides() && !current.prepared_slides_need_time());
  const auto before_factory_reset = stored;
  fail_erase = true;
  assert(reset_configuration(ResetMode::Factory).error && stored == before_factory_reset);
  fail_erase = false;
  fail_commit = true;
  assert(reset_configuration(ResetMode::Factory).error && stored == before_factory_reset);
  fail_commit = false;
  assert(!reset_configuration(ResetMode::Factory).error);
  assert(stored.size() == 1 && stored.contains("settings_v1"));
  assert(current.read() && current.ssid.empty() && current.password.empty() && current.web_password.empty());
  assert(!current.has_immich() && !current.has_prepared_slides() && current.reset_epoch == 2);
  assert(current.screen_rotation == ScreenRotation::Normal && current.brightness_day == 64);
  assert(!Settings::save_playback(R"({"immich_url":"http://old.test","immich_key":"old-key"})"));
  assert(!Settings::save(R"({"immich_url":"http://photos.test","immich_key":"fresh-key"})"));
  const auto after_factory_reset = stored;
  assert(!Settings::save_wifi(*new_wifi, 1));
  assert(!Settings::save(R"({"ssid":"Photo Lab","password":"old-password","immich_url":"http://old.test",
      "immich_key":"stale-key","web_password":"old-settings"})", 1));
  assert(stored == after_factory_reset);
  assert(Settings::save_wifi(*new_wifi, 2));
  assert(current.read() && current.ssid == "New Wi-Fi" && current.web_password.empty() && !current.has_immich());
  assert(Settings::save(R"({"immich_url":"http://fresh.test","immich_key":"fresh-key","web_password":"new-settings"})", 2));
  assert(current.read() && current.has_immich() && current.web_password == "new-settings" && current.reset_epoch == 2);
  assert(update_configuration(R"({"reset_epoch":1,"values":{"brightness_day":88}})").error);
  assert(!reset_configuration(ResetMode::Factory).error);
  assert(Settings::save(R"({"ssid":"Brand new","password":"","immich_url":"http://fresh.test",
      "immich_key":"fresh-key","web_password":"new-settings"})", 3));
  assert(current.read() && current.ssid == "Brand new" && current.has_immich() && current.reset_epoch == 3);

  const auto reference = stored;
  assert(!Settings::save_playback(R"({"immich_url":"http://fresh.test","photo_source":"custom","person_ids":"missing"})"));
  assert(stored == reference);
  assert(Settings::save_playback(R"({"immich_url":"http://fresh.test","photo_source":"custom","albums_enabled":true,
      "album_ids":"C48E2980-18AC-4D7D-876A-A636759A1AEC, 0406C468-DABF-4777-BB93-AC6FDAC62857",
      "location_enabled":true,"filter_city":"São Paulo","rating_enabled":true,"minimum_rating":"4+"})"));
  assert(current.read() && current.photo_source.mode == PhotoSourceMode::Custom && current.reset_epoch == 3);
  assert(current.photo_filters.criteria.album_ids == "c48e2980-18ac-4d7d-876a-a636759a1aec,0406c468-dabf-4777-bb93-ac6fdac62857");
  assert(current.photo_filters.criteria.city == "São Paulo" && current.photo_filters.criteria.minimum_rating == 4);
  assert(!update_configuration(R"({"values":{"people_enabled":true,"person_ids":"22222222-2222-4222-8222-222222222222"}})").error);
  assert(current.read() && current.photo_filters.criteria.people_enabled);
  assert(Settings::save_playback(R"({"immich_url":"http://fresh.test","photo_source":"memories"})"));
  assert(current.read() && current.photo_source.mode == PhotoSourceMode::Memories && current.photo_filters.criteria.people_enabled);

  const Json large_update(cJSON_CreateObject(), cJSON_Delete);
  auto *large_values = cJSON_AddObjectToObject(large_update.get(), "values");
  std::string ids;
  for (char last : std::string_view("012345")) {
    if (!ids.empty())
      ids += ',';
    ids += "0406c468-dabf-4777-bb93-ac6fdac6285";
    ids += last;
  }
  for (auto key_name : {"album_ids", "person_ids", "tag_ids", "excluded_album_ids", "excluded_person_ids", "excluded_tag_ids"})
    assert(cJSON_AddStringToObject(large_values, key_name, ids.c_str()));
  for (auto key_name : {"filter_country", "filter_state", "filter_city"})
    assert(cJSON_AddStringToObject(large_values, key_name, std::string(96, '"').c_str()));
  assert(cJSON_AddStringToObject(large_values, "immich_url", endpoint.c_str()));
  assert(cJSON_AddStringToObject(large_values, "api_key", std::string(256, 'k').c_str()));
  const std::unique_ptr<char, decltype(&cJSON_free)> serialized(cJSON_PrintUnformatted(large_update.get()), cJSON_free);
  assert(serialized && !update_configuration(serialized.get()).error);
  assert(stored_blobs.contains("settings_v1") && current.read() && current.photo_filters.criteria.excluded_tag_ids == ids);
  assert(current.photo_filters.criteria.city == std::string(96, '"'));
  assert(!reset_configuration(ResetMode::Customization).error);
  assert(!stored_blobs.contains("settings_v1") && current.read());
  assert(current.ssid == "Brand new" && current.photo_filters.criteria.excluded_tag_ids.empty() && current.reset_epoch == 4);

  using espframe::native::PhotoOrientation;
  assert(Settings::save_playback(R"({"immich_url":"http://fresh.test","immich_key":"fresh-key","portrait_pairs_only":true,
      "photo_orientation":"Landscape Only","screen_rotation":"180"})"));
  assert(current.read() && current.photo_orientation == PhotoOrientation::Landscape);
  assert(!current.requires_portrait_pair() && current.portrait_pairing && current.portrait_pairs_only);
  assert(current.screen_rotation == ScreenRotation::UpsideDown);
  const auto oriented = stored;
  assert(update_configuration(R"({"values":{"photo_orientation":"90","brightness_day":35}})").error);
  assert(stored == oriented);
  assert(!update_configuration(R"({"values":{"photo_orientation":"Portrait Only"}})").error);
  assert(current.read() && current.photo_orientation == PhotoOrientation::Portrait && current.requires_portrait_pair());
  const Json orientation_snapshot(configuration_snapshot(current), cJSON_Delete);
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(
      cJSON_GetObjectItemCaseSensitive(orientation_snapshot.get(), "values"), "photo_orientation")) ==
      std::string_view("Portrait Only"));
  const auto enable_developer = update_configuration(R"({"values":{"developer_features_enabled":true}})");
  assert(!enable_developer.error && !enable_developer.restart_required);
  assert(current.read() && current.developer_features_enabled && current.screen_rotation == ScreenRotation::UpsideDown);
  using espframe::native::active_screen_brightness;
  using espframe::native::active_screen_schedule;
  using espframe::native::ScreenBrightness;
  using espframe::native::ScreenScheduleSettings;
  const auto live_display = update_configuration(R"({"values":{"brightness_day":72,"brightness_night":54,
      "schedule_enabled":true,"schedule_on_hour":19,"schedule_off_hour":7,"schedule_wake_timeout":61}})");
  assert(!live_display.error && !live_display.restart_required);
  assert((active_screen_brightness.load() == ScreenBrightness{72, 54}));
  const auto awake_hours = ScreenScheduleSettings::unpack(active_screen_schedule.load());
  assert(awake_hours.enabled && awake_hours.on_hour == 19 && awake_hours.off_hour == 7 && awake_hours.wake_seconds == 61);
  assert(current.read() && current.brightness_day == 72 && current.night_brightness() == 54);
  fail_commit = true;
  assert(update_configuration(R"({"values":{"brightness_day":76,"schedule_wake_timeout":600}})").error);
  fail_commit = false;
  assert((active_screen_brightness.load() == ScreenBrightness{72, 54}));
  assert(active_screen_schedule.load() == awake_hours.packed());
  const auto selection_before_rotation = espframe::native::photo_selection_revision.load();
  const auto rotate = update_configuration(R"({"values":{"screen_rotation":"90","portrait_pairing":true}})");
  assert(!rotate.error && !rotate.restart_required);
  assert(espframe::native::requested_screen_rotation.load() == ScreenRotation::Clockwise);
  assert(espframe::native::photo_selection_revision.load() == selection_before_rotation);
  assert(current.read() && current.screen_rotation == ScreenRotation::Clockwise && !current.portrait_pairing);
  assert(!update_configuration(R"({"values":{"portrait_pairing":true}})").error);
  assert(current.read() && !current.portrait_pairing && !current.requires_portrait_pair());
  fail_commit = true;
  assert(update_configuration(R"({"values":{"screen_rotation":"270"}})").error);
  fail_commit = false;
  assert(espframe::native::requested_screen_rotation.load() == ScreenRotation::Clockwise);
  assert(!update_configuration(R"({"values":{"screen_rotation":"270"}})").error);
  assert(espframe::native::requested_screen_rotation.load() == ScreenRotation::Counterclockwise);
  assert(current.read() && current.screen_rotation == ScreenRotation::Counterclockwise);
  const Json rotated_snapshot(configuration_snapshot(current), cJSON_Delete);
  const auto *rotated_values = cJSON_GetObjectItemCaseSensitive(rotated_snapshot.get(), "values");
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(rotated_values, "developer_features_enabled")));
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(rotated_values, "screen_rotation")) == std::string_view("270"));
  const auto disable_developer = update_configuration(R"({"values":{"developer_features_enabled":false}})");
  assert(!disable_developer.error && !disable_developer.restart_required);
  assert(espframe::native::requested_screen_rotation.load() == ScreenRotation::Normal);
  assert(current.read() && !current.developer_features_enabled && current.screen_rotation == ScreenRotation::Normal &&
         !current.portrait_pairing);
  // A stock backup may restore the fields in any order.
  assert(!update_configuration(R"({"values":{"screen_rotation":"90","portrait_pairing":true,
                                             "developer_features_enabled":true}})").error);
  assert(current.read() && current.screen_rotation == ScreenRotation::Clockwise && !current.portrait_pairing);
  assert(!reset_configuration(ResetMode::Customization).error);
  assert(current.read() && current.photo_orientation == PhotoOrientation::Any &&
         !current.developer_features_enabled && current.screen_rotation == ScreenRotation::Normal);

  using espframe::native::decode_identity_form;
  using espframe::native::make_frame_identity;
  assert(decode_identity_form("name=++Salon+%C3%A9t%C3%A9%26co+").value() == "Salon été&co");
  assert(decode_identity_form("name=").value().empty());
  assert(!decode_identity_form("name=duplicate&name=other"));
  assert(!decode_identity_form("name=bad%00name"));
  assert(!decode_identity_form("name=bad%C0%AF"));
  assert(!decode_identity_form("name=bad%"));
  assert(!decode_identity_form("name=" + std::string(121, 'a')));
  const espframe::native::FrameMac mac{0x02, 0xab, 0xcd, 0xef, 0x12, 0x9a};
  const auto named = make_frame_identity("Salon été&co", mac, "immich-frame-10inch", "Espframe 10inch");
  assert(named.friendly_name == "Salon été&co" && named.hostname == "salon-t-co-129a" && named.mac_suffix == "129a");
  const auto unnamed = make_frame_identity({}, mac, "immich-frame-10inch", "Espframe 10inch");
  assert(unnamed.friendly_name == "Espframe 10inch" && unnamed.hostname == "immich-frame-10inch");
  assert(make_frame_identity("✨", mac, "immich-frame-10inch", "Espframe 10inch").hostname == "frame-129a");
  assert(make_frame_identity("Living Room", {}, "immich-frame-10inch", "Espframe 10inch").mac_suffix.empty());

  const auto renamed = update_configuration(R"({"values":{"frame_name":"  Salon été&co  "}})");
  assert(!renamed.error && renamed.frame_name == "Salon été&co");
  assert(current.read() && current.frame_name == named.name);
  assert(Settings::save(R"({"immich_url":"http://fresh.test","immich_key":"fresh-key","web_password":"new-settings"})"));
  assert(current.read() && current.frame_name == named.name);  // Older USB setup omits the name.
  assert(current.pair_framing == espframe::native::PairFraming::FollowPhotos && current.pair_display_mode() == DisplayMode::Fit);
  assert(!update_configuration(R"({"values":{"display_mode":"Fit","portrait_pair_framing":"Fill"}})").error);
  assert(current.read() && current.display_mode == DisplayMode::Fit && current.pair_display_mode() == DisplayMode::Fill);
  assert(Settings::save(R"({"immich_url":"http://fresh.test","immich_key":"fresh-key","web_password":"new-settings",
      "display_mode":"Fit"})"));
  assert(current.read() && current.display_mode == DisplayMode::Fit && current.pair_display_mode() == DisplayMode::Fill);
  assert(current.pair_divider == espframe::native::PairDivider::Black);
  const auto before_divider = espframe::native::photo_source_revision.load();
  const auto before_divider_selection = espframe::native::photo_selection_revision.load();
  const auto white_divider = update_configuration(R"({"values":{"portrait_pair_divider":"White"}})");
  assert(!white_divider.error && !white_divider.restart_required && espframe::native::photo_source_revision.load() == before_divider + 1);
  assert(espframe::native::photo_selection_revision.load() == before_divider_selection);
  assert(current.read() && current.pair_divider == espframe::native::PairDivider::White);
  assert(Settings::save(R"({"immich_url":"http://fresh.test","immich_key":"fresh-key","web_password":"new-settings",
      "display_mode":"Fit"})"));
  assert(current.read() && current.pair_divider == espframe::native::PairDivider::White);
  const auto prior_framing = stored;
  assert(update_configuration(R"({"values":{"portrait_pair_divider":"Grey"}})").error);
  assert(stored == prior_framing);
  assert(update_configuration(R"({"values":{"portrait_pair_framing":"Stretch"}})").error);
  assert(stored == prior_framing);
  assert(!update_configuration(R"({"values":{"display_mode":"Fill","portrait_pair_framing":"Fit"}})").error);
  assert(current.read() && current.display_mode == DisplayMode::Fill && current.pair_display_mode() == DisplayMode::Fit);
  assert(!update_configuration(R"({"values":{"portrait_pair_framing":"Follow photo framing"}})").error);
  assert(current.read() && current.pair_display_mode() == DisplayMode::Fill);
  const Json named_snapshot(configuration_snapshot(current), cJSON_Delete);
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(
      cJSON_GetObjectItemCaseSensitive(named_snapshot.get(), "values"), "frame_name")) == std::string_view(named.name));
  const auto before_bad_name = stored;
  assert(update_configuration(R"({"values":{"frame_name":"bad\u0000name","brightness_day":55}})").error);
  assert(!Settings::save(R"({"immich_url":"http://fresh.test","immich_key":"fresh-key","frame_name":"bad\u0000name"})"));
  assert(stored == before_bad_name);
  assert(Settings::save(R"({"immich_url":"http://fresh.test","immich_key":"fresh-key","web_password":"new-settings",
      "frame_name":"literal\\u0000"})"));
  assert(current.read() && current.frame_name == R"(literal\u0000)");
  assert(!update_configuration(R"({"values":{"frame_name":""}})").error);
  assert(current.read() && current.frame_name.empty());

  constexpr char ha_key[] = "AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA=";
  const auto decoded_key = espframe::native::decode_home_assistant_key(ha_key);
  assert(decoded_key && decoded_key->front() == 1 && decoded_key->back() == 32);
  assert(!espframe::native::decode_home_assistant_key("AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyB="));
  assert(!espframe::native::decode_home_assistant_key("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="));
  const auto before_bad_ha_key = stored;
  assert(!Settings::save(R"({"immich_url":"http://fresh.test","immich_key":"fresh-key","home_assistant_key":"invalid"})"));
  assert(stored == before_bad_ha_key);
  const std::string usb_pairing = std::string(R"({"immich_url":"http://fresh.test","immich_key":"fresh-key",
      "web_password":"new-settings","home_assistant_key":")") + ha_key + R"("})";
  assert(Settings::save(usb_pairing.c_str()));
  assert(current.read() && current.home_assistant_key == ha_key);
  assert(!Settings::save_slide_interval(10, ""));
  assert(!Settings::save_slide_interval(10, "previous-pairing"));
  assert(!Settings::save_slide_interval(1, ha_key));
  assert(Settings::save_slide_interval(45, ha_key));
  assert(current.read() && current.slide_seconds == 45 && current.home_assistant_key == ha_key);
  assert(active_slideshow_interval.load() == 45);
  const auto before_interval_failure = stored;
  fail_commit = true;
  assert(Settings::save_slide_interval(45, ha_key));  // No flash write for an unchanged value.
  assert(!Settings::save_slide_interval(60, ha_key));
  fail_commit = false;
  assert(stored == before_interval_failure && active_slideshow_interval.load() == 45);
  assert(current.read() && current.slide_seconds == 45);
  const Json ha_snapshot(configuration_snapshot(current), cJSON_Delete);
  const auto *interval_values = cJSON_GetObjectItemCaseSensitive(ha_snapshot.get(), "values");
  assert(std::string_view(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(interval_values, "interval"))) ==
         "45 seconds");
  const std::unique_ptr<char, decltype(&cJSON_free)> serialized_ha(cJSON_PrintUnformatted(ha_snapshot.get()), cJSON_free);
  assert(serialized_ha && std::string_view(serialized_ha.get()).find(ha_key) == std::string_view::npos);
  assert(update_configuration(R"({"values":{"home_assistant_key":""}})").error);

  const auto interval_revision = espframe::native::photo_source_revision.load();
  const auto web_interval = update_configuration(R"({"values":{"interval":"17 seconds"}})");
  assert(!web_interval.error && web_interval.updated == 1 && !web_interval.restart_required);
  assert(current.read() && current.slide_seconds == 17 && active_slideshow_interval.load() == 17);
  bool restart_for_interval = true;
  assert(Settings::save_playback(R"({"immich_url":"http://fresh.test","slide_seconds":20})", &restart_for_interval));
  assert(!restart_for_interval && current.read() && current.slide_seconds == 20 && active_slideshow_interval.load() == 20);
  fail_commit = true;
  assert(update_configuration(R"({"values":{"interval":"30 seconds"}})").error);
  fail_commit = false;
  assert(current.read() && current.slide_seconds == 20 && active_slideshow_interval.load() == 20);
  assert(espframe::native::photo_source_revision.load() == interval_revision);
  assert(!current.slideshow_progress_ring_enabled);
  const auto ring_on = update_configuration(R"({"values":{"slideshow_progress_ring_enabled":true}})");
  assert(!ring_on.error && ring_on.updated == 1 && !ring_on.restart_required);
  assert(current.read() && current.slideshow_progress_ring_enabled &&
         espframe::native::active_slideshow_progress_ring.load());
  const auto ring_off = update_configuration(R"({"values":{"slideshow_progress_ring_enabled":false}})");
  assert(!ring_off.error && ring_off.updated == 1 && !ring_off.restart_required);
  assert(current.read() && !current.slideshow_progress_ring_enabled &&
         !espframe::native::active_slideshow_progress_ring.load());

  using espframe::native::PhotoPreset;
  using espframe::native::active_photo_preset;
  using espframe::native::photo_selection_revision;
  using espframe::native::photo_source_revision;
  assert(Settings::save_playback(R"({"immich_url":"http://fresh.test","person_ids":"22222222-2222-4222-8222-222222222222",
      "tags_enabled":true,"tag_ids":"33333333-3333-4333-8333-333333333333","excluded_tag_ids":"44444444-4444-4444-8444-444444444444"})"));
  assert(!Settings::save_photo_preset(PhotoPreset::Person, "previous-pairing"));
  const auto original_revision = photo_source_revision.load();
  assert(Settings::save_photo_preset(PhotoPreset::Person, ha_key));
  assert(photo_source_revision.load() == original_revision + 1 && active_photo_preset.load() == PhotoPreset::Person);
  assert(photo_selection_revision.load() == original_revision + 1);
  assert(current.read() && current.photo_source.mode == PhotoSourceMode::Custom &&
         current.photo_source.preset() == PhotoPreset::Person && current.photo_filters.criteria.people_enabled);
  assert(!current.photo_filters.criteria.tags_enabled && current.photo_filters.criteria.excluded_tag_ids.empty());
  assert(!current.photo_filters.criteria.tag_ids.empty());
  fail_commit = true;
  assert(Settings::save_photo_preset(PhotoPreset::Person, ha_key));
  assert(!Settings::save_photo_preset(PhotoPreset::Memories, ha_key));
  fail_commit = false;
  assert(photo_source_revision.load() == original_revision + 1 && active_photo_preset.load() == PhotoPreset::Person);
  assert(Settings::save_slide_interval(30, ha_key));
  assert(current.read() && current.photo_source.preset() == PhotoPreset::Person);
  assert(Settings::save_photo_preset(PhotoPreset::Tag, ha_key));
  assert(current.read() && current.photo_source.preset() == PhotoPreset::Tag && current.photo_filters.criteria.tags_enabled);
  assert(!current.photo_filters.criteria.people_enabled && !current.photo_filters.criteria.person_ids.empty());
  assert(Settings::save_playback(R"({"immich_url":"http://fresh.test","filter_city":"London","display_mode":"Fit"})"));
  assert(current.read() && current.photo_source.preset() == PhotoPreset::Custom && current.display_mode == DisplayMode::Fit);

  const auto before_live = photo_source_revision.load();
  const auto live = update_configuration(R"({"values":{"portrait_pair_framing":"Fill","photo_orientation":"Portrait Only",
      "photo_metadata_location_enabled":false,"date_filter_enabled":true,"date_from":"2024-03-01"}})");
  assert(!live.error && !live.restart_required && photo_source_revision.load() == before_live + 1);
  assert(photo_selection_revision.load() == before_live + 1);
  assert(!Settings::take_live_photo_feed(before_live));
  fail_commit = true;
  assert(update_configuration(R"({"values":{"portrait_pair_framing":"Fit"}})").error);
  fail_commit = false;
  assert(photo_source_revision.load() == before_live + 1);
  const auto mixed = update_configuration(R"({"values":{"frame_name":"Other name","photo_orientation":"Landscape Only"}})");
  assert(!mixed.error && mixed.restart_required && photo_source_revision.load() == before_live + 1);
  const auto feed = Settings::take_live_photo_feed(before_live + 1);
  assert(feed && feed->pair_display_mode() == DisplayMode::Fill && feed->capture_dates.from == "2024-03-01");
  assert(feed->photo_orientation == PhotoOrientation::Portrait && !feed->metadata.show_location);
  assert(!Settings::take_live_photo_feed(before_live + 1));
  assert(current.read() && current.photo_orientation == PhotoOrientation::Landscape);
  bool restart_required = true;
  assert(Settings::save_playback(R"({"immich_url":"http://fresh.test","frame_name":"Other name","display_mode":"Fill"})",
                                 &restart_required));
  assert(!restart_required && photo_source_revision.load() == before_live + 2);
  assert(photo_selection_revision.load() == before_live + 1);
  assert(Settings::save_playback(R"({"immich_url":"http://fresh.test","frame_name":"Other name","display_mode":"Fill"})",
                                 &restart_required));
  assert(!restart_required && photo_source_revision.load() == before_live + 2);
  assert(Settings::save_playback(R"({"immich_url":"http://fresh.test","immich_key":"replacement-key","display_mode":"Fit"})",
                                 &restart_required));
  assert(!restart_required && photo_source_revision.load() == before_live + 3);
  assert(photo_selection_revision.load() == before_live + 3);
  assert(!Settings::take_live_photo_feed(before_live + 2));
  fail_commit = true;
  assert(update_configuration(R"({"values":{"immich_url":"https://failed.test","api_key":"failed-key"}})").error);
  fail_commit = false;
  assert(!update_configuration(R"({"values":{"immich_url":"http://fresh.test/api/"}})").restart_required);
  assert(photo_source_revision.load() == before_live + 3);

  const auto before_queued_saves = photo_source_revision.load();
  assert(!update_configuration(R"({"values":{"photo_orientation":"Any"}})").restart_required);
  assert(!update_configuration(R"({"values":{"portrait_pair_framing":"Fit"}})").restart_required);
  assert(photo_source_revision.load() == before_queued_saves + 2);
  assert(photo_selection_revision.load() == before_queued_saves + 1);
  assert(!Settings::take_live_photo_feed(before_queued_saves + 1));
  const auto coalesced = Settings::take_live_photo_feed(before_queued_saves + 2);
  assert(coalesced && coalesced->photo_orientation == PhotoOrientation::Any &&
         coalesced->pair_display_mode() == DisplayMode::Fit);
  assert(coalesced->pending_connection && coalesced->pending_connection->immich_url == "http://fresh.test/api" &&
         coalesced->pending_connection->immich_key == "replacement-key");
  assert(!Settings::take_live_photo_feed(before_queued_saves + 2));

  const auto replacement = update_configuration(
      R"({"values":{"immich_url":"https://new-photos.test/immich","api_key":"new-server-key"}})");
  assert(!replacement.error && !replacement.restart_required);
  const auto new_revision = photo_source_revision.load();
  assert(new_revision == before_queued_saves + 3 && photo_selection_revision.load() == new_revision);
  const auto reconnected = Settings::take_live_photo_feed(new_revision);
  assert(reconnected && reconnected->pending_connection &&
         reconnected->pending_connection->immich_url == "https://new-photos.test/immich/api" &&
         reconnected->pending_connection->immich_key == "new-server-key");

  using esphome::espframe::SlideTransition;
  using espframe::native::active_clock_twelve_hour;
  using espframe::native::active_playback_transition;
  const auto photo_revision_before_appearance = photo_source_revision.load();
  const auto appearance = update_configuration(R"({"values":{"clock_format":"12 Hour","transition":"Slide Left",
      "transition_speed":"Slow"}})");
  assert(!appearance.error && !appearance.restart_required && active_clock_twelve_hour.load());
  const auto slow_slide = active_playback_transition.load();
  assert(slow_slide.effect == SlideTransition::SLIDE_LEFT && slow_slide.duration_ms == 1500 && !slow_slide.random);
  assert(current.read() && current.clock.twelve_hour() && current.transition == "Slide Left");
  assert(photo_source_revision.load() == photo_revision_before_appearance);

  fail_commit = true;
  assert(update_configuration(R"({"values":{"clock_format":"24 Hour","transition_speed":"Fast"}})").error);
  fail_commit = false;
  assert(active_clock_twelve_hour.load() && active_playback_transition.load().duration_ms == 1500);

  assert(Settings::save_playback(R"({"immich_url":"http://fresh.test","clock_format":"24 Hour","transition":"Random",
      "transition_speed":"Fast","display_mode":"Fill"})", &restart_required));
  assert(!restart_required && !active_clock_twelve_hour.load());
  const auto fast_random = active_playback_transition.load();
  assert(fast_random.random && fast_random.duration_ms == 500);
  assert(photo_source_revision.load() == photo_revision_before_appearance + 1);
  const auto mixed_photo_feed = Settings::take_live_photo_feed(photo_revision_before_appearance + 1);
  assert(mixed_photo_feed && mixed_photo_feed->display_mode == DisplayMode::Fill);

  const auto clock_timezone = update_configuration(R"json({"values":{"clock_format":"12 Hour",
      "timezone":"America/New_York (GMT-5)","show_clock":true}})json");
  assert(!clock_timezone.error && !clock_timezone.restart_required && active_clock_twelve_hour.load());
  assert(current.read() && current.clock.twelve_hour() && current.clock.show);
  assert(espframe::native::active_clock_visible.load());
  const auto *new_york = espframe::native::find_clock_zone("America/New_York (GMT-5)");
  assert(new_york && espframe::native::requested_clock_zone.load() == new_york);
  assert(photo_source_revision.load() == photo_revision_before_appearance + 2);
  assert(Settings::take_live_photo_feed(photo_revision_before_appearance + 2));
  assert(active_clock_twelve_hour.load() && active_playback_transition.load().random);

  auto ntp_revision = espframe::native::ntp_settings_revision.load();
  const auto ntp_change = update_configuration(R"({"values":{"ntp_server_1":" time.example.test ",
      "ntp_server_2":"","ntp_server_3":"2001:db8::5"}})");
  assert(!ntp_change.error && !ntp_change.restart_required);
  const auto expected_servers = std::array<std::string, 3>{"time.example.test", "", "2001:db8::5"};
  assert(current.read() && current.clock.ntp_servers == expected_servers);
  assert(espframe::native::updated_ntp_servers(ntp_revision) == expected_servers);
  assert(!espframe::native::updated_ntp_servers(ntp_revision));
  const Json ntp_snapshot(configuration_snapshot(current), cJSON_Delete);
  const auto *ntp_values = cJSON_GetObjectItemCaseSensitive(ntp_snapshot.get(), "values");
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(ntp_values, "ntp_server_1")) ==
         expected_servers[0]);
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(ntp_values, "ntp_server_2")) ==
         expected_servers[1]);
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(ntp_values, "ntp_server_3")) ==
         expected_servers[2]);
  assert(update_configuration(R"({"values":{"ntp_server_2":"https://other.test"}})").error);
  assert(current.read() && current.clock.ntp_servers == expected_servers);
  fail_commit = true;
  assert(update_configuration(R"json({"values":{"ntp_server_2":"time2.example.test",
      "timezone":"Europe/London (GMT+0)","show_clock":false}})json").error);
  fail_commit = false;
  assert(!espframe::native::updated_ntp_servers(ntp_revision));
  assert(espframe::native::active_clock_visible.load() && espframe::native::requested_clock_zone.load() == new_york);

  using espframe::native::active_connection_timeout;
  assert(active_connection_timeout.load() == 600);
  const auto timeout_change = update_configuration(R"({"values":{"conn_timeout":"45 seconds"}})");
  assert(!timeout_change.error && !timeout_change.restart_required && active_connection_timeout.load() == 45);
  assert(current.read() && current.connection_timeout_seconds == 45);
  const Json timeout_snapshot(configuration_snapshot(current), cJSON_Delete);
  const auto *timeout_values = cJSON_GetObjectItemCaseSensitive(timeout_snapshot.get(), "values");
  assert(std::string_view(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(timeout_values, "conn_timeout"))) ==
         "45 seconds");
  assert(update_configuration(R"({"values":{"conn_timeout":"7 seconds"}})").error);
  assert(update_configuration(R"({"values":{"conn_timeout":45}})").error);
  fail_commit = true;
  assert(update_configuration(R"({"values":{"conn_timeout":"5 minutes"}})").error);
  fail_commit = false;
  assert(current.read() && current.connection_timeout_seconds == 45 && active_connection_timeout.load() == 45);
  const auto before_ha_timeout = photo_source_revision.load();
  assert(!Settings::save_connection_timeout(90, ha_key));
  assert(!Settings::save_connection_timeout(180, "previous-pairing"));
  assert(Settings::save_connection_timeout(180, ha_key));
  fail_commit = true;
  assert(Settings::save_connection_timeout(180, ha_key));
  assert(!Settings::save_connection_timeout(300, ha_key));
  fail_commit = false;
  assert(current.read() && current.connection_timeout_seconds == 180 && active_connection_timeout.load() == 180);
  assert(photo_source_revision.load() == before_ha_timeout);
  assert(Settings::save_connection_timeout(45, ha_key));

  assert(!current.needs_local_date());
  const auto live_time_settings = [&](const char *request, bool needs_time) {
    const auto saved = update_configuration(request);
    assert(!saved.error && !saved.restart_required);
    const auto updated = Settings::take_live_photo_feed(photo_source_revision.load());
    assert(updated && updated->needs_local_date() == needs_time);
  };
  live_time_settings(R"({"values":{"photo_metadata_date_format":"Relative Date"}})", true);
  live_time_settings(R"({"values":{"photo_metadata_date_enabled":false}})", false);
  live_time_settings(R"({"values":{"date_filter_enabled":true,"date_filter_mode":"Relative Range"}})", true);
  live_time_settings(R"({"values":{"date_filter_enabled":false}})", false);
  assert(Settings::save_photo_preset(PhotoPreset::Memories, ha_key));
  const auto memories = Settings::take_live_photo_feed(photo_source_revision.load());
  assert(memories && memories->photo_source.mode == PhotoSourceMode::Memories && memories->needs_local_date());

  assert(Settings::save_wifi(*new_wifi, current.reset_epoch));
  assert(Settings::save(R"({"immich_url":"http://fresh.test","immich_key":"fresh-key","web_password":"new-settings"})"));
  assert(current.read() && current.home_assistant_key == ha_key);
  assert(current.clock.ntp_servers == expected_servers);
  assert(current.connection_timeout_seconds == 45);

  assert(!update_configuration(R"({"values":{"frame_name":"Restored frame"}})").error);
  assert(!reset_configuration(ResetMode::Customization).error);
  assert(current.read() && current.frame_name.empty() && current.home_assistant_key == ha_key);
  assert(Settings::save(R"({"immich_url":"http://fresh.test","immich_key":"fresh-key","web_password":"new-settings",
      "home_assistant_key":""})"));
  assert(current.read() && current.home_assistant_key.empty());
  assert(!Settings::save_slide_interval(10, ha_key));
  assert(Settings::save(usb_pairing.c_str()));
  assert(!update_configuration(R"({"values":{"frame_name":"Before factory reset"}})").error);
  assert(!reset_configuration(ResetMode::Factory).error);
  assert(current.read() && current.frame_name.empty() && current.home_assistant_key.empty());

  const auto save_filter_frame = [](const char *source) {
    const std::string data = R"({"ssid":"Test","password":"","immich_url":"http://fresh.test","immich_key":"fresh-key",
        "web_password":"new-settings","photo_source":")" + std::string(source) +
        R"(","album_id":"11111111-1111-4111-8111-111111111111"})";
    assert(Settings::save(data.c_str()));
  };
  const auto apply_shared_filters = [&](const char *request) {
    const auto revision = photo_source_revision.load();
    const auto applied = update_configuration(request);
    assert(!applied.error && !applied.restart_required && photo_source_revision.load() == revision + 1);
    assert(espframe::native::photo_selection_revision.load() == revision + 1);
    const auto feed = Settings::take_live_photo_feed(revision + 1);
    assert(feed && current.read() && feed->photo_source.mode == current.photo_source.mode);
    return *feed;
  };

  save_filter_frame("all");
  const auto filtered = apply_shared_filters(R"({"values":{"people_enabled":true,
      "person_ids":"22222222-2222-4222-8222-222222222222","favorites_enabled":true,
      "favorite_mode":"Exclude favorites"}})");
  assert(filtered.photo_source.mode == PhotoSourceMode::Custom);
  assert(filtered.photo_filters.criteria.people_enabled &&
         filtered.photo_filters.criteria.person_ids == "22222222-2222-4222-8222-222222222222" &&
         filtered.photo_filters.criteria.favorites_enabled &&
         filtered.photo_filters.criteria.favorite_mode == "Exclude favorites");
  assert(active_photo_preset.load() == PhotoPreset::Custom);
  const auto label_revision = photo_source_revision.load();
  auto label_save = update_configuration(R"({"values":{"person_labels":"[\"Family, Friends\"]"}})");
  assert(!label_save.error && !label_save.restart_required && photo_source_revision.load() == label_revision);
  assert(current.read());
  const Json labeled(configuration_snapshot(current), cJSON_Delete);
  const auto *labeled_values = cJSON_GetObjectItemCaseSensitive(labeled.get(), "values");
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(labeled_values, "person_labels")) ==
         std::string_view(R"(["Family, Friends"])"));
  const auto labeled_document = stored;
  const std::string oversized_label = "{\"values\":{\"person_labels\":\"" + std::string(256, 'x') + "\"}}";
  assert(update_configuration(oversized_label.c_str()).error && stored == labeled_document);
  label_save = update_configuration(R"({"values":{"person_labels":""}})");
  assert(!label_save.error && !label_save.restart_required && photo_source_revision.load() == label_revision);
  assert(current.read() && current.photo_filters.labels[1].empty());

  save_filter_frame("album");
  assert(current.read() && !current.photo_filters.criteria.albums_enabled &&
         current.photo_filters.criteria.album_ids.empty());
  const Json album_snapshot(configuration_snapshot(current), cJSON_Delete);
  const auto *album_values = cJSON_GetObjectItemCaseSensitive(album_snapshot.get(), "values");
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(album_values, "albums_enabled")));
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(album_values, "album_ids")) == current.photo_source.album_id);
  const auto album_revision = photo_source_revision.load();
  const auto before_noop = stored;
  assert(!update_configuration(R"({"values":{"albums_enabled":true,
      "album_ids":"11111111-1111-4111-8111-111111111111"}})").error);
  assert(stored == before_noop && photo_source_revision.load() == album_revision);
  assert(update_configuration(R"({"values":{"albums_enabled":false,"rating_enabled":"true"}})").error);
  assert(stored == before_noop && photo_source_revision.load() == album_revision);
  const auto composed_album = apply_shared_filters(R"({"values":{"location_enabled":true,"filter_city":"London"}})");
  assert(composed_album.photo_source.mode == PhotoSourceMode::Custom && composed_album.photo_filters.criteria.albums_enabled);
  assert(composed_album.photo_filters.criteria.album_ids == "11111111-1111-4111-8111-111111111111");
  assert(composed_album.photo_filters.criteria.location_enabled && composed_album.photo_filters.criteria.city == "London");

  save_filter_frame("album");
  const auto memories_with_album = apply_shared_filters(R"({"values":{"photo_source":"Memories"}})");
  assert(memories_with_album.photo_source.mode == PhotoSourceMode::Memories);
  assert(memories_with_album.photo_filters.criteria.albums_enabled &&
         memories_with_album.photo_filters.criteria.album_ids == "11111111-1111-4111-8111-111111111111");
  const auto resumed_filters = apply_shared_filters(R"({"values":{"photo_source":"Custom"}})");
  assert(resumed_filters.photo_source.mode == PhotoSourceMode::Custom && resumed_filters.photo_filters.criteria.albums_enabled);
  const auto person_preset = apply_shared_filters(R"({"values":{"photo_source":"Person",
      "person_ids":"22222222-2222-4222-8222-222222222222","person_labels":"Family"}})");
  assert(person_preset.photo_source.preset() == PhotoPreset::Person && person_preset.photo_filters.criteria.people_enabled);
  assert(!person_preset.photo_filters.criteria.albums_enabled && person_preset.photo_filters.labels[1] == "Family");

  save_filter_frame("album");
  assert(Settings::save_playback(R"({"immich_url":"http://fresh.test",
      "album_ids":"33333333-3333-4333-8333-333333333333"})"));
  const auto saved_list = apply_shared_filters(R"({"values":{"location_enabled":true,"filter_city":"London"}})");
  assert(saved_list.photo_filters.criteria.album_ids == "33333333-3333-4333-8333-333333333333");
  assert(saved_list.photo_source.album_id == "11111111-1111-4111-8111-111111111111");

  const auto standalone = apply_shared_filters(R"({"values":{"photo_source_mode":"album",
      "album_id":"AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA"}})");
  assert(standalone.photo_source.mode == PhotoSourceMode::Album);
  assert(standalone.photo_source.album_id == "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
  assert(standalone.photo_filters.criteria.album_ids == saved_list.photo_filters.criteria.album_ids);
  assert(current.read());
  const Json standalone_snapshot(configuration_snapshot(current), cJSON_Delete);
  const auto *standalone_values = cJSON_GetObjectItemCaseSensitive(standalone_snapshot.get(), "values");
  assert(std::string_view(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(standalone_values, "photo_source_mode"))) == "album");
  assert(std::string_view(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(standalone_values, "album_id"))) ==
         standalone.photo_source.album_id);
  assert(update_configuration(R"({"values":{"photo_source_mode":"album","album_id":"invalid"}})").error);
  assert(update_configuration(R"({"values":{"photo_source_mode":"all","photo_source":"Memories"}})").error);
  const auto restored_custom = apply_shared_filters(R"({"values":{"photo_source_mode":"custom"}})");
  assert(restored_custom.photo_source.mode == PhotoSourceMode::Custom && restored_custom.photo_filters.criteria.location_enabled);

  save_filter_frame("favorites");
  assert(current.read() && !current.photo_filters.criteria.favorites_enabled);
  const Json favorites_snapshot(configuration_snapshot(current), cJSON_Delete);
  const auto *favorites_values = cJSON_GetObjectItemCaseSensitive(favorites_snapshot.get(), "values");
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(favorites_values, "favorites_enabled")));
  assert(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(favorites_values, "favorite_mode")) ==
         std::string_view("Favorites only"));
  const auto no_favorites = apply_shared_filters(R"({"values":{"favorites_enabled":false}})");
  assert(no_favorites.photo_source.mode == PhotoSourceMode::Library && !no_favorites.photo_filters.criteria.favorites_enabled);

  save_filter_frame("memories");
  const auto memories_revision = photo_source_revision.load();
  const auto dormant = update_configuration(R"({"values":{"favorites_enabled":true,"favorite_mode":"Favorites only"}})");
  assert(!dormant.error && !dormant.restart_required && photo_source_revision.load() == memories_revision);
  assert(current.read() && current.photo_source.mode == PhotoSourceMode::Memories &&
         current.photo_filters.criteria.favorites_enabled);
  assert(Settings::save_playback(R"({"immich_url":"http://fresh.test","photo_source":"all","favorites_enabled":true})"));
  assert(current.read() && current.photo_source.mode == PhotoSourceMode::Library &&
         current.photo_filters.criteria.favorites_enabled);

  using espframe::native::frame_status_snapshot;
  using espframe::native::FrameStatusStore;
  using espframe::native::ImmichResponse;
  using espframe::native::InitialPhotoTimeout;
  using espframe::native::immich_response;
  using espframe::native::PhotoSourceStatus;
  FrameStatusStore status;
  assert(std::string_view(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(capabilities.get(), "status_path"))) ==
         "/espframe/api/v1/status");
  assert(std::string_view(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(capabilities.get(), "screen_control_path"))) ==
         "/espframe/api/v1/screen");
  const auto expect_status = [&](bool online, bool restarting, const char *screen, const char *playback,
                                 const char *source, const char *immich, const char *notice, bool pending = false) {
    const Json output(frame_status_snapshot(status.read(), online, restarting, pending), cJSON_Delete);
    assert(cJSON_GetArraySize(output.get()) == 8);
    const auto field = [&](const char *name) {
      const auto *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(output.get(), name));
      assert(value);
      return std::string_view(value);
    };
    assert(field("screen") == screen && field("playback") == playback && field("source") == source);
    assert(field("immich") == immich && field("notice") == notice);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(output.get(), "restarting")) == restarting);
    const auto *command = cJSON_GetObjectItemCaseSensitive(output.get(), "screen_pending");
    assert(cJSON_IsBool(command) && (cJSON_IsTrue(command) != 0) == pending);
  };
  expect_status(false, false, "starting", "setup", "none", "not_configured", "none");
  status.configure(true, PhotoSourceMode::Memories);
  expect_status(false, false, "starting", "loading", "memories", "waiting_for_network", "none");
  status.set_screen(true);
  expect_status(true, false, "awake", "loading", "memories", "connecting", "none");
  status.finish_preparation(PhotoSourceStatus::MemoriesUnavailable, immich_response(401));
  expect_status(true, false, "awake", "loading", "memories", "rejected", "memories_unavailable");
  assert(immich_response(403) == ImmichResponse::Rejected && immich_response(503) == ImmichResponse::RequestFailed);
  status.finish_preparation(PhotoSourceStatus::None, immich_response(0));
  expect_status(true, false, "awake", "loading", "memories", "unreachable", "none");
  status.finish_preparation(PhotoSourceStatus::None, immich_response(200));
  expect_status(true, false, "awake", "loading", "memories", "responding", "none");
  status.presented_photo();
  status.finish_preparation(PhotoSourceStatus::WaitingForPair, ImmichResponse::Pending);
  expect_status(true, false, "awake", "showing", "memories", "responding", "waiting_for_pair");
  expect_status(true, false, "awake", "showing", "memories", "responding", "waiting_for_pair", true);
  status.set_screen(false);
  expect_status(true, true, "asleep", "paused", "memories", "responding", "waiting_for_pair");

  constexpr int64_t second = 1'000'000;
  InitialPhotoTimeout initial;
  assert(initial.notice(60 * second, 30) == ImmichResponse::Pending);
  initial.observe(65 * second, false, ImmichResponse::Unreachable);
  initial.observe(70 * second, false, ImmichResponse::Pending);
  initial.observe(80 * second, false, ImmichResponse::Rejected);
  assert(initial.notice(95 * second - 1, 30) == ImmichResponse::Pending);
  assert(initial.notice(95 * second, 30) == ImmichResponse::Rejected);
  assert(initial.notice(95 * second, 45) == ImmichResponse::Pending);
  initial.observe(96 * second, false, ImmichResponse::Responding);
  assert(initial.notice(100 * second, 30) == ImmichResponse::Pending);
  initial.observe(100 * second, false, ImmichResponse::RequestFailed);
  assert(initial.notice(129 * second, 30) == ImmichResponse::Pending);
  assert(initial.notice(130 * second, 30) == ImmichResponse::RequestFailed);
  initial.observe(131 * second, true, ImmichResponse::Pending);
  assert(initial.notice(160 * second, 30) == ImmichResponse::Pending);

  using espframe::native::active_warm_tone_override;
  assert(Settings::save(R"({"ssid":"Photo Lab","password":"","immich_url":"http://photos.test",
      "immich_key":"tone-key","web_password":"tone-settings","warm_tone_override":true})"));
  assert(Settings::save(R"({"immich_url":"http://photos.test","immich_key":"tone-key","web_password":"tone-settings"})"));
  assert(current.read() && current.tone.night_override);
  current.publish_live_presentation();
  assert(active_warm_tone_override.load());
  const auto before_tone = photo_source_revision.load();
  const auto before_tone_selection = photo_selection_revision.load();
  const auto tone_change = update_configuration(R"({"values":{"base_tone_enabled":true,"base_tone":25,
      "warm_tone_intensity":65,"warm_tone_override":true}})");
  assert(!tone_change.error && !tone_change.restart_required && photo_source_revision.load() == before_tone + 1);
  const auto tinted_feed = Settings::take_live_photo_feed(before_tone + 1);
  assert(tinted_feed && tinted_feed->tone.night_override && tinted_feed->tone.night_amount == 65);
  assert(current.read() && current.tone.night_override);
  const Json tone_snapshot(configuration_snapshot(current), cJSON_Delete);
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(tone_snapshot.get(), "values"),
                                                    "warm_tone_override")));
  assert(update_configuration(R"({"values":{"warm_tone_override":"true"}})").error);
  assert(!Settings::expire_warm_tone_override("Different timezone", *tinted_feed));
  fail_commit = true;
  assert(!Settings::expire_warm_tone_override(current.clock.timezone, *tinted_feed));
  fail_commit = false;
  assert(active_warm_tone_override.load() && photo_source_revision.load() == before_tone + 1);
  const auto awaiting_restart = update_configuration(
      R"({"values":{"frame_name":"Restarting frame","brightness_day":70,"favorites_enabled":true}})");
  assert(!awaiting_restart.error && awaiting_restart.restart_required);
  assert(Settings::expire_warm_tone_override(current.clock.timezone, *tinted_feed));
  assert(!active_warm_tone_override.load() && photo_source_revision.load() == before_tone + 2);
  assert(photo_selection_revision.load() == before_tone_selection);
  const auto after_tone = Settings::take_live_photo_feed(before_tone + 2);
  assert(after_tone && !after_tone->tone.night_override && after_tone->tone.base_enabled);
  assert(after_tone->photo_source.mode == tinted_feed->photo_source.mode && !after_tone->photo_filters.criteria.favorites_enabled);
  assert(current.read() && !current.tone.night_override && current.tone.night_amount == 65);
  assert(current.brightness_day == 70 && current.photo_filters.criteria.favorites_enabled);
  assert(!Settings::expire_warm_tone_override(current.clock.timezone, *after_tone));

  const auto before_language = photo_source_revision.load();
  const auto selection_before_language = photo_selection_revision.load();
  const auto new_language = update_configuration(R"({"values":{"device_language":"he"}})");
  assert(!new_language.error && !new_language.restart_required);
  assert(espframe::native::active_device_language.load() == find_espframe_language("he"));
  assert(photo_source_revision.load() == before_language + 1 && photo_selection_revision.load() == selection_before_language);
  const auto localized_feed = Settings::take_live_photo_feed(before_language + 1);
  assert(localized_feed && localized_feed->metadata.language == find_espframe_language("he"));
  assert(update_configuration(R"({"values":{"device_language":"unknown"}})").error);
  assert(Settings::save(R"({"immich_url":"http://photos.test","immich_key":"tone-key","web_password":"tone-settings"})"));
  assert(current.read() && current.metadata.language == find_espframe_language("he"));
  const Json language_snapshot(configuration_snapshot(current), cJSON_Delete);
  assert(std::string_view(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(
      cJSON_GetObjectItemCaseSensitive(language_snapshot.get(), "values"), "device_language"))) == "he");

  const auto paired_language_settings = std::string(R"({"immich_url":"http://photos.test","immich_key":"tone-key",
      "web_password":"tone-settings","home_assistant_key":")") + ha_key + R"("})";
  assert(Settings::save(paired_language_settings.c_str()));
  const auto before_ha_language = photo_source_revision.load();
  const auto selections_before_ha_language = photo_selection_revision.load();
  const auto french = static_cast<uint8_t>(find_espframe_language("fr"));
  const auto german = static_cast<uint8_t>(find_espframe_language("de"));
  assert(!Settings::save_device_language(255, ha_key));
  assert(!Settings::save_device_language(french, "previous-pairing"));
  assert(Settings::save_device_language(french, ha_key));
  fail_commit = true;
  assert(Settings::save_device_language(french, ha_key));
  assert(!Settings::save_device_language(german, ha_key));
  fail_commit = false;
  assert(current.read() && current.metadata.language == french && espframe::native::active_device_language.load() == french);
  assert(photo_source_revision.load() == before_ha_language + 1);
  assert(photo_selection_revision.load() == selections_before_ha_language);
  const auto ha_language_feed = Settings::take_live_photo_feed(before_ha_language + 1);
  assert(ha_language_feed && ha_language_feed->metadata.language == french);

  assert(Settings::save(R"({"immich_url":"http://photos.test","immich_key":"tone-key","web_password":"tone-settings",
      "firmware_updates":{"auto_update":false,"update_frequency":"Weekly","wifi_auto_update":true}})"));
  assert(Settings::save(R"({"immich_url":"http://photos.test","immich_key":"tone-key","web_password":"tone-settings",
      "firmware_updates":{"wifi_auto_update":false}})"));
  assert(Settings::save(R"({"immich_url":"http://photos.test","immich_key":"tone-key","web_password":"tone-settings"})"));
  assert(!Settings::save(R"({"immich_url":"http://photos.test","immich_key":"tone-key","firmware_updates":{"auto_update":"false"}})"));
  assert(!Settings::save(R"({"immich_url":"http://photos.test","immich_key":"tone-key","firmware_updates":{"update_frequency":"Yearly"}})"));
  assert(!update_configuration(R"({"values":{"clock_format":"12 Hour"}})").error);
  assert(current.read());
  const auto &stock_updates = current.stock_update_preferences;
  assert(stock_updates.auto_update == false && stock_updates.frequency == "Weekly" && stock_updates.wifi_auto_update == false);
  const Json update_backup(cJSON_CreateObject(), cJSON_Delete);
  assert(current.add_stock_update_preferences(update_backup.get()));
  const auto *update_fields = cJSON_GetObjectItemCaseSensitive(update_backup.get(), "firmware_updates");
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(update_fields, "auto_update")));
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(update_fields, "wifi_auto_update")));
  const auto *frequency = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(update_fields, "update_frequency"));
  assert(frequency && std::string_view(frequency) == "Weekly");
  const auto update_preferences = update_configuration(R"({"values":{"auto_update":true,"update_frequency":"Hourly"}})");
  assert(!update_preferences.error && !update_preferences.restart_required);
  assert(current.read() && current.stock_update_preferences.auto_update.value_or(false) &&
         current.stock_update_preferences.frequency == "Hourly" && current.stock_update_preferences.wifi_auto_update == false);
  assert(update_configuration(R"({"values":{"update_frequency":"On every boot"}})").error);
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> update_snapshot(configuration_snapshot(current), cJSON_Delete);
  const auto *update_values = cJSON_GetObjectItemCaseSensitive(update_snapshot.get(), "values");
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(update_values, "auto_update")));
  assert(std::string_view(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(update_values, "update_frequency"))) == "Hourly");
  assert(Settings::reset(ResetMode::Customization));
  assert(current.read() && current.stock_update_preferences.frequency == "Hourly");
  assert(Settings::reset(ResetMode::Factory));
  assert(current.read() && !current.stock_update_preferences.frequency && !current.stock_update_preferences.auto_update);

  assert(Settings::save(R"({"ssid":"Test Wi-Fi","password":"test-wifi-key","immich_url":"https://photos.test",
      "immich_key":"test-immich-key","web_password":"test-admin-password"})"));
  assert(Settings::save(R"({"immich_url":"https://photos.test","immich_key":"test-immich-key","slide_seconds":30})"));
  assert(current.read() && current.web_password == "test-admin-password" && current.slide_seconds == 30);
  assert(Settings::save(R"({"immich_url":"https://photos.test","immich_key":"test-immich-key","web_password":"replacement-admin"})"));
  assert(current.read() && current.web_password == "replacement-admin");
  assert(update_configuration(R"({"values":{"photo_metadata_date_enabled":false}})").error == nullptr);
  assert(!Settings::save(R"({"web_password":""})", current.reset_epoch + 1));
  assert(Settings::save(R"({"web_password":""})", current.reset_epoch));
  assert(current.read() && current.web_password.empty() && current.ssid == "Test Wi-Fi" &&
         current.immich_key == "test-immich-key" && !current.metadata.show_date);
  assert(update_configuration(R"({"values":{"photo_metadata_date_enabled":true}})").error == nullptr);
  assert(current.read() && current.metadata.show_date);
  assert(Settings::reset(ResetMode::Customization));
  assert(current.read() && current.web_password.empty() && current.ssid == "Test Wi-Fi");

  verify_stock_pairing_migration();
  verify_stock_connection_migration();
  verify_setup_framing();

  assert(Settings::save(R"({"immich_url":"http://photos.test","immich_key":"wifi-key","web_password":"wifi-settings",
      "firmware_updates":{"auto_update":false,"update_frequency":"Weekly","wifi_auto_update":false}})"));
  const auto wifi_preference = update_configuration(R"({"values":{"c6_auto_update":true}})");
  assert(!wifi_preference.error && !wifi_preference.restart_required);
  assert(update_configuration(R"({"values":{"c6_auto_update":"false"}})").error);
  assert(current.read() && current.stock_update_preferences.wifi_auto_update == true &&
         current.stock_update_preferences.auto_update == false && current.stock_update_preferences.frequency == "Weekly");
  const Json wifi_snapshot(configuration_snapshot(current), cJSON_Delete);
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(wifi_snapshot.get(), "values"),
                                                     "c6_auto_update")));
  const auto wireguard = update_configuration(R"({"values":{"wireguard_enabled":true,
      "wireguard_endpoint":"vpn.example.test","wireguard_port":51821,"wireguard_address":"10.10.0.10",
      "wireguard_netmask":"255.255.255.0","wireguard_allowed_ips":"10.10.0.25/24; 10.10.0.1",
      "wireguard_peer_public_key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
      "wireguard_private_key":"BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBA="}})");
  assert(!wireguard.error && !wireguard.restart_required);
  assert(update_configuration(R"({"values":{"wireguard_port":0,"wireguard_enabled":false}})").error);
  assert(update_configuration(R"({"values":{"wireguard_private_key":"invalid"}})").error);
  assert(current.read() && current.wireguard.enabled && current.wireguard.port == 51821 &&
         current.wireguard.allowed_ips == "10.10.0.0/24, 10.10.0.1/32");
  const auto saved_tunnel = current.wireguard;
  const Json tunnel_snapshot(configuration_snapshot(current), cJSON_Delete);
  const auto *tunnel_values = cJSON_GetObjectItemCaseSensitive(tunnel_snapshot.get(), "values");
  const auto *tunnel_secrets = cJSON_GetObjectItemCaseSensitive(tunnel_snapshot.get(), "secrets_configured");
  assert(!cJSON_HasObjectItem(tunnel_values, "wireguard_private_key") &&
         cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(tunnel_secrets, "wireguard_private_key")));
  assert(Settings::save(R"({"immich_url":"http://photos.test","immich_key":"wifi-key","web_password":"wifi-settings"})"));
  assert(current.read() && current.wireguard == saved_tunnel);
  assert(!update_configuration(R"({"values":{"wireguard_private_key":""}})").error);
  assert(current.read() && current.wireguard.private_key.empty() && current.wireguard.endpoint == "vpn.example.test");
  assert(Settings::reset(ResetMode::Customization));
  assert(current.read() && !current.wireguard.enabled && current.wireguard.endpoint.empty());
  const auto stored = Settings::storage_revision();
  assert(!update_configuration(R"({"values":{"brightness_day":70}})").error);
  assert(Settings::storage_revision() == stored + 1);
  assert(!update_configuration(R"({"values":{"brightness_day":70}})").error);
  assert(Settings::storage_revision() == stored + 1);
  fail_commit = true;
  assert(update_configuration(R"({"values":{"brightness_day":75}})").error);
  fail_commit = false;
  assert(Settings::storage_revision() == stored + 1);

  espframe::native::FrameStatusStore live_status;
  assert(!live_status.solar_times());
  live_status.set_solar_times(espframe::native::SolarTimes{390, 1145});
  const Json solar_snapshot(configuration_snapshot(current, live_status.solar_times()), cJSON_Delete);
  const auto *solar_values = cJSON_GetObjectItemCaseSensitive(solar_snapshot.get(), "values");
  assert(std::string_view(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(solar_values, "sunrise"))) == "6:30 AM");
  assert(std::string_view(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(solar_values, "sunset"))) == "7:05 PM");
  live_status.set_solar_times({});
  const Json syncing_snapshot(configuration_snapshot(current, live_status.solar_times()), cJSON_Delete);
  const auto *syncing_values = cJSON_GetObjectItemCaseSensitive(syncing_snapshot.get(), "values");
  assert(std::string_view(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(syncing_values, "sunrise"))).empty());
  assert(update_configuration(R"({"values":{"sunrise":"6:10 AM"}})").error);
}
