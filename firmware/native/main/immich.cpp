#include "immich.h"

#include "preview_decoder.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <ctime>
#include <memory>

namespace espframe::native {
namespace {

constexpr const char *tag = "immich";
constexpr EventBits_t got_ip = BIT0;
constexpr size_t photo_capacity = 2 * 1024 * 1024;
constexpr size_t search_capacity = 64 * 1024;
constexpr size_t batch_size = 16;

bool valid_asset_id(const char *id) {
  if (id == nullptr || std::strlen(id) != 36)
    return false;
  for (int i = 0; i < 36; ++i) {
    const char c = id[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-')
        return false;
    } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool ImmichSource::read_settings() {
  return this->settings_.read();
}

void ImmichSource::wifi_event_(void *argument, esp_event_base_t base, int32_t event, void *) {
  auto &source = *static_cast<ImmichSource *>(argument);
  if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP)
    xEventGroupSetBits(source.wifi_events_, got_ip);
  else if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED)
    xEventGroupClearBits(source.wifi_events_, got_ip);
}

bool ImmichSource::start_wifi_() {
  this->wifi_events_ = xEventGroupCreate();
  if (this->wifi_events_ == nullptr || esp_netif_init() != ESP_OK ||
      esp_event_loop_create_default() != ESP_OK || esp_netif_create_default_wifi_sta() == nullptr)
    return false;

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  init.nvs_enable = 0;
  if (esp_wifi_init(&init) != ESP_OK || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
      esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event_, this) != ESP_OK ||
      esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_, this) != ESP_OK)
    return false;

  wifi_config_t station{};
  std::memcpy(station.sta.ssid, this->settings_.ssid.data(), this->settings_.ssid.size());
  std::memcpy(station.sta.password, this->settings_.password.data(), this->settings_.password.size());
  station.sta.threshold.authmode = this->settings_.password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  return esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK &&
         esp_wifi_set_config(WIFI_IF_STA, &station) == ESP_OK && esp_wifi_start() == ESP_OK;
}

bool ImmichSource::online_() {
  if (!this->wifi_started_) {
    this->wifi_started_ = true;
    this->wifi_ready_ = this->start_wifi_();
    if (!this->wifi_ready_)
      ESP_LOGW(tag, "Could not initialize Guition Wi-Fi");
  }
  if (!this->wifi_ready_)
    return false;
  if ((xEventGroupGetBits(this->wifi_events_) & got_ip) == 0) {
    esp_wifi_connect();
    if (!(xEventGroupWaitBits(this->wifi_events_, got_ip, pdFALSE, pdFALSE, pdMS_TO_TICKS(8'000)) & got_ip))
      return false;
  }
  if (this->settings_.immich_url.starts_with("https://") && std::time(nullptr) < 1'690'000'000) {
    if (!this->sntp_started_) {
      esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
      this->sntp_started_ = esp_netif_sntp_init(&config) == ESP_OK;
    }
    if (!this->sntp_started_ || esp_netif_sntp_sync_wait(pdMS_TO_TICKS(3'000)) != ESP_OK)
      return false;
  }
  return true;
}

int ImmichSource::fetch_(Request request, const char *asset, size_t limit, size_t &received) {
  received = 0;
  const bool search = request == Request::Search;
  std::string url = this->settings_.immich_url;
  if (search)
    url += "/search/random";
  else
    url += std::string("/assets/") + asset + "/thumbnail?size=preview";

  if (this->http_ == nullptr) {
    esp_http_client_config_t config{};
    config.url = url.c_str();
    config.disable_auto_redirect = true;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 3000;
    config.buffer_size = 4096;
    this->http_ = esp_http_client_init(&config);
    if (this->http_ == nullptr)
      return 0;
    if (esp_http_client_set_header(this->http_, "x-api-key", this->settings_.immich_key.c_str()) != ESP_OK) {
      esp_http_client_cleanup(this->http_);
      this->http_ = nullptr;
      return 0;
    }
  }

  const std::string body = search ? "{\"type\":\"IMAGE\",\"size\":" + std::to_string(batch_size) + "}" : "";
  if (esp_http_client_set_url(this->http_, url.c_str()) != ESP_OK ||
      esp_http_client_set_method(this->http_, search ? HTTP_METHOD_POST : HTTP_METHOD_GET) != ESP_OK ||
      esp_http_client_set_header(this->http_, "Content-Type", "application/json") != ESP_OK)
    return 0;
  if (esp_http_client_open(this->http_, body.size()) != ESP_OK) {
    esp_http_client_close(this->http_);
    return 0;
  }

  const auto request_deadline = esp_timer_get_time() + 12'000'000;
  const int status = [&]() {
    if (search && esp_http_client_write(this->http_, body.data(), body.size()) !=
                      static_cast<int>(body.size()))
      return 0;
    const int64_t content_length = esp_http_client_fetch_headers(this->http_);
    if (content_length < 0 && !esp_http_client_is_chunked_response(this->http_))
      return 0;
    const int code = esp_http_client_get_status_code(this->http_);
    if (code != 200)
      return code;
    if (content_length > static_cast<int64_t>(limit))
      return 413;

    // A complete parser state can still have buffered bytes from the
    // header read. Drain those bytes before trusting the completion flag.
    while (esp_timer_get_time() < request_deadline) {
      char overflow;
      const bool at_limit = received == limit;
      char *target = at_limit ? &overflow : reinterpret_cast<char *>(this->encoded_ + received);
      const int count = esp_http_client_read(this->http_, target, at_limit ? 1 : std::min<size_t>(limit - received, 4096));
      if (count == 0)
        return received > 0 && esp_http_client_is_complete_data_received(this->http_) ? code : 0;
      if (count < 0)
        return 0;
      if (at_limit)
        return 413;
      received += count;
    }
    return 0;
  }();
  esp_http_client_close(this->http_);
  return status;
}

bool ImmichSource::refill_() {
  size_t length;
  const int status = this->fetch_(Request::Search, nullptr, search_capacity, length);
  if (status != 200) {
    ESP_LOGW(tag, "Photo search failed (%d)", status);
    return false;
  }
  this->encoded_[length] = 0;
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> photos(
      cJSON_ParseWithLengthOpts(reinterpret_cast<const char *>(this->encoded_), length + 1, nullptr, true),
      cJSON_Delete);
  if (!cJSON_IsArray(photos.get()))
    return false;

  const cJSON *photo;
  cJSON_ArrayForEach(photo, photos.get()) {
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(photo, "id"));
    if (!valid_asset_id(id))
      continue;
    AssetId asset{};
    std::memcpy(asset.data(), id, asset.size());
    if (std::find(this->pending_.begin(), this->pending_.end(), asset) == this->pending_.end())
      this->pending_.push_back(asset);
    if (this->pending_.size() == batch_size)
      break;
  }
  // Prefer fresh results without stalling a library smaller than one batch.
  const auto cutoff = this->history_.end() - std::min<size_t>(this->history_.size(), batch_size);
  std::stable_partition(this->pending_.begin(), this->pending_.end(), [&](const AssetId &id) {
    return std::find(cutoff, this->history_.end(), id) != this->history_.end();
  });
  return !this->pending_.empty();
}

bool ImmichSource::next_asset_(AssetId &id) {
  if (this->pending_.empty() && !this->refill_())
    return false;
  id = this->pending_.back();
  this->pending_.pop_back();
  return true;
}

bool ImmichSource::prepare(int slide, uint16_t *panel) {
  const size_t index = slide - 1;
  if (index > this->history_.size() || !this->online_())
    return false;
  if (this->encoded_ == nullptr) {
    this->encoded_ = static_cast<uint8_t *>(heap_caps_malloc(photo_capacity, MALLOC_CAP_SPIRAM));
    if (this->encoded_ == nullptr)
      return false;
  }
  const bool new_position = index == this->history_.size();
  const bool remembered = !new_position && this->history_[index][0] != '\0';
  AssetId id{};
  if (remembered)
    id = this->history_[index];
  else if (!this->next_asset_(id))
    return false;

  const auto request_us = esp_timer_get_time();
  size_t length;
  const int status = this->fetch_(Request::Preview, id.data(), photo_capacity, length);
  const auto decode_us = esp_timer_get_time();
  const bool ready = status == 200 && decode_preview(this->encoded_, length, panel);
  if (ready) {
    if (new_position)
      this->history_.push_back(id);
    else if (!remembered)
      this->history_[index] = id;
    ESP_LOGI(tag, "Slide %d prepared, preview_bytes=%zu fetch_us=%" PRId64 " decode_us=%" PRId64,
             slide, length, decode_us - request_us, esp_timer_get_time() - decode_us);
    return true;
  }
  // Keep a staged photo across transient failures. A missing or invalid
  // preview can be skipped when it has never been presented.
  if (status == 404 || status == 413 || status == 200) {
    if (remembered) {
      // A photo from history may disappear from Immich after eviction.
      // Fill that position with another real photo on its next attempt.
      this->history_[index] = {};
    }
  } else if (!remembered) {
    this->pending_.push_back(id);
  }
  ESP_LOGW(tag, "Photo preview was unavailable or could not be decoded (%d)", status);
  return false;
}

}  // namespace espframe::native
