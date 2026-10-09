#include "reset_coordinator.h"

#include <nvs.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <string>
#include <vector>

#include "esphome/components/json/json_util.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "configuration_contract_generated.h"

namespace esphome::espframe {
namespace {
constexpr char TAG[] = "espframe.reset";
constexpr char RESET_NAMESPACE[] = "espframe_rs";
constexpr char RESET_RECORD_KEY[] = "state";
// This persistent override keeps compiled stations disabled on every later
// boot so ESPHome continues loading credentials from its fallback preference.
constexpr char FACTORY_WIFI_RESET_KEY[] = "wifi_reset";
constexpr uint32_t RESET_RECORD_VERSION = 1;
constexpr uint32_t WIFI_FALLBACK_PREFERENCE_KEY = 88491487UL;
constexpr uint32_t API_NOISE_PREFERENCE_KEY = 88491486UL;

struct ResetRecord {
  uint32_t version{RESET_RECORD_VERSION};
  uint32_t mode{0};
  uint32_t epoch{0};
};

struct PreferenceEntry {
  std::string name_space;
  std::string key;
};

bool collect_entries(std::vector<PreferenceEntry> &entries) {
  nvs_iterator_t iterator = nullptr;
  esp_err_t result = nvs_entry_find("nvs", nullptr, NVS_TYPE_ANY, &iterator);
  if (result == ESP_ERR_NVS_NOT_FOUND) return true;
  if (result != ESP_OK) return false;
  while (iterator != nullptr) {
    nvs_entry_info_t info{};
    nvs_entry_info(iterator, &info);
    entries.push_back({info.namespace_name, info.key});
    result = nvs_entry_next(&iterator);
    if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) {
      nvs_release_iterator(iterator);
      return false;
    }
  }
  return true;
}

std::string preference_key(uint32_t id) { return std::to_string(id); }

uint32_t wifi_preference_id() {
  uint32_t id = WIFI_FALLBACK_PREFERENCE_KEY;
  if (wifi::global_wifi_component != nullptr && wifi::global_wifi_component->has_sta()) {
    id = App.get_config_version_hash();
  }
  return id;
}

bool keep_customization_preference(const PreferenceEntry &entry) {
  if (entry.name_space != "esphome") return false;
  const uint32_t wifi_id = wifi_preference_id();
  return entry.key == preference_key(wifi_id) || entry.key == preference_key(wifi_id + 1) ||
         entry.key == preference_key(API_NOISE_PREFERENCE_KEY);
}

bool is_preserved_entry(const PreferenceEntry &entry, ResetMode mode) {
  if (entry.name_space == RESET_NAMESPACE) return true;
  return mode == ResetMode::CUSTOMIZATION && keep_customization_preference(entry);
}
}  // namespace

bool ResetCoordinator::load_() {
  nvs_handle_t handle;
  esp_err_t result = nvs_open(RESET_NAMESPACE, NVS_READONLY, &handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return true;
  if (result != ESP_OK) return false;
  ResetRecord record{};
  size_t length = sizeof(record);
  result = nvs_get_blob(handle, RESET_RECORD_KEY, &record, &length);
  nvs_close(handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return true;
  if (result != ESP_OK || length != sizeof(record) || record.version != RESET_RECORD_VERSION ||
      record.mode > static_cast<uint32_t>(ResetMode::FACTORY)) return false;
  this->mode_ = static_cast<ResetMode>(record.mode);
  this->epoch_ = record.epoch;
  return true;
}

bool ResetCoordinator::save_() {
  nvs_handle_t handle;
  esp_err_t result = nvs_open(RESET_NAMESPACE, NVS_READWRITE, &handle);
  if (result != ESP_OK) return false;
  ResetRecord record{RESET_RECORD_VERSION, static_cast<uint32_t>(this->mode_), this->epoch_};
  result = nvs_set_blob(handle, RESET_RECORD_KEY, &record, sizeof(record));
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
}

bool ResetCoordinator::clear_factory_wifi_reset() {
  nvs_handle_t handle;
  esp_err_t result = nvs_open(RESET_NAMESPACE, NVS_READWRITE, &handle);
  if (result != ESP_OK) return false;
  result = nvs_erase_key(handle, FACTORY_WIFI_RESET_KEY);
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK || result == ESP_ERR_NVS_NOT_FOUND;
}

namespace {
bool factory_wifi_reset_pending() {
  nvs_handle_t handle;
  esp_err_t result = nvs_open(RESET_NAMESPACE, NVS_READONLY, &handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return false;
  if (result != ESP_OK) {
    ESP_LOGW(TAG, "Could not read factory WiFi reset state: %s", esp_err_to_name(result));
    return true;
  }
  uint8_t pending = 0;
  result = nvs_get_u8(handle, FACTORY_WIFI_RESET_KEY, &pending);
  nvs_close(handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return false;
  if (result != ESP_OK) {
    ESP_LOGW(TAG, "Could not read factory WiFi reset flag: %s", esp_err_to_name(result));
    return true;
  }
  return pending != 0;
}

bool save_factory_wifi_reset() {
  nvs_handle_t handle;
  esp_err_t result = nvs_open(RESET_NAMESPACE, NVS_READWRITE, &handle);
  if (result != ESP_OK) return false;
  result = nvs_set_u8(handle, FACTORY_WIFI_RESET_KEY, 1);
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
}
}  // namespace

bool ResetCoordinator::clear_preferences_(ResetMode mode) {
  std::vector<PreferenceEntry> entries;
  if (!collect_entries(entries)) return false;
  for (const auto &entry : entries) {
    if (is_preserved_entry(entry, mode)) continue;
    nvs_handle_t handle;
    esp_err_t result = nvs_open(entry.name_space.c_str(), NVS_READWRITE, &handle);
    if (result != ESP_OK) return false;
    result = nvs_erase_key(handle, entry.key.c_str());
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) return false;
  }

  entries.clear();
  if (!collect_entries(entries)) return false;
  for (const auto &entry : entries) {
    if (!is_preserved_entry(entry, mode)) {
      ESP_LOGE(TAG, "Storage cleanup verification found %s/%s", entry.name_space.c_str(), entry.key.c_str());
      return false;
    }
  }
  return true;
}

void ResetCoordinator::setup() {
  if (!this->load_()) {
    this->failed_ = true;
    ESP_LOGE(TAG, "Could not read reset state; settings were left untouched");
    return;
  }
  // Apply a previous factory reset before choosing which preferences a
  // customization reset keeps. Compiled stations would otherwise select the
  // config-hash key and cause the provisioned fallback credentials to be erased.
  if (factory_wifi_reset_pending() && wifi::global_wifi_component != nullptr) {
    ESP_LOGI(TAG, "Suppressing WiFi credentials compiled into firmware after factory reset");
    wifi::global_wifi_component->clear_sta();
  }
  if (this->mode_ == ResetMode::NONE) return;
  ESP_LOGW(TAG, "Resuming pending %s reset", this->mode_ == ResetMode::FACTORY ? "factory" : "customization");
  if (!this->clear_preferences_(this->mode_)) {
    this->failed_ = true;
    ESP_LOGE(TAG, "Reset cleanup failed; pending request retained for retry");
    return;
  }
  if (this->mode_ == ResetMode::FACTORY && !save_factory_wifi_reset()) {
    this->failed_ = true;
    ESP_LOGE(TAG, "Could not persist factory WiFi reset state; pending request retained for retry");
    return;
  }
  this->mode_ = ResetMode::NONE;
  ++this->epoch_;
  if (!this->save_()) {
    this->failed_ = true;
    ESP_LOGE(TAG, "Reset completed but could not persist completion state");
    return;
  }
  if (factory_wifi_reset_pending() && wifi::global_wifi_component != nullptr) {
    ESP_LOGI(TAG, "Suppressing WiFi credentials compiled into firmware after factory reset");
    wifi::global_wifi_component->clear_sta();
  }
  ESP_LOGI(TAG, "Reset completed; reset epoch is %lu", static_cast<unsigned long>(this->epoch_));
}

bool ResetCoordinator::request(ResetMode mode) {
  if (mode == ResetMode::NONE || this->firmware_update_in_progress_ || this->c6_update_in_progress_ ||
      this->settings_update_in_progress_) return false;
  if (this->failed_) {
    if (this->mode_ != ResetMode::NONE && this->mode_ != mode) return false;
    if (this->mode_ == ResetMode::NONE) {
      this->mode_ = mode;
      if (!this->save_()) {
        this->mode_ = ResetMode::NONE;
        return false;
      }
    }
    this->failed_ = false;
    return true;
  }
  if (this->mode_ != ResetMode::NONE) return this->mode_ == mode;
  this->mode_ = mode;
  if (!this->save_()) {
    this->mode_ = ResetMode::NONE;
    return false;
  }
  return true;
}

bool ResetApiHandler::canHandle(AsyncWebServerRequest *request) const {
  char url_buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef url = request->url_to(url_buffer);
  return url == contract::RESET_PATH &&
         (request->method() == HTTP_GET || request->method() == HTTP_POST);
}

void ResetApiHandler::handleRequest(AsyncWebServerRequest *request) {
  if (request->method() == HTTP_GET) {
    json::JsonBuilder builder;
    JsonObject root = builder.root();
    root["status"] = this->coordinator_->failed() ? "failed" :
                      this->coordinator_->pending() ? "pending" : "ready";
    root["epoch"] = this->coordinator_->epoch();
    root["mode"] = this->coordinator_->mode() == ResetMode::FACTORY ? "factory" :
                    this->coordinator_->mode() == ResetMode::CUSTOMIZATION ? "customization" : "none";
    auto payload = builder.serialize();
    request->send(200, "application/json", payload.c_str());
    return;
  }

  // Browsers send Origin on POST. Require it to match Host so another site
  // cannot trigger an irreversible reset through a cross-origin form/fetch.
  auto origin_header = request->get_header("Origin");
  auto host_header = request->get_header("Host");
  if (!origin_header.has_value() || !host_header.has_value()) {
    request->send(403, "application/json", R"({"status":"rejected","error":"same_origin_required"})");
    return;
  }
  const std::string &origin = origin_header.value();
  const size_t scheme_end = origin.find("://");
  if (scheme_end == std::string::npos || scheme_end == 0 || origin.substr(scheme_end + 3) != host_header.value()) {
    request->send(403, "application/json", R"({"status":"rejected","error":"cross_origin"})");
    return;
  }

  if (!request->hasArg("mode")) {
    request->send(400, "application/json", R"({"status":"rejected","error":"missing_mode"})");
    return;
  }
  const std::string mode_arg = request->arg("mode");
  const ResetMode mode = mode_arg == "customization" ? ResetMode::CUSTOMIZATION :
                         mode_arg == "factory" ? ResetMode::FACTORY : ResetMode::NONE;
  if (mode == ResetMode::NONE) {
    request->send(400, "application/json", R"({"status":"rejected","error":"invalid_mode"})");
    return;
  }
  if (this->coordinator_->firmware_update_in_progress() || this->coordinator_->c6_update_in_progress() ||
      this->coordinator_->settings_update_in_progress()) {
    request->send(409, "application/json", R"({"status":"rejected","error":"operation_in_progress"})");
    return;
  }
  if (!this->coordinator_->request(mode)) {
    request->send(409, "application/json", R"({"status":"rejected","error":"reset_unavailable"})");
    return;
  }
  request->send(200, "application/json", R"({"status":"accepted"})");
  vTaskDelay(pdMS_TO_TICKS(250));
  esp_restart();
}

}  // namespace esphome::espframe
