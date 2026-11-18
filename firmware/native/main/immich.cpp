#include "immich.h"

#include "preview_decoder.h"
#include "web_settings.h"

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

namespace espframe::native {
namespace {

constexpr const char *tag = "immich";
constexpr EventBits_t got_ip = BIT0;
constexpr size_t photo_capacity = 2 * 1024 * 1024;
constexpr size_t search_capacity = 64 * 1024;
constexpr int64_t pairing_time_us = 1'500'000;

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
  if (this->wifi_events_ == nullptr || esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK)
    return false;
  auto *netif = esp_netif_create_default_wifi_sta();
  if (netif == nullptr)
    return false;
  esp_netif_set_hostname(netif, "espframe-native");

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
  start_web_settings(this->settings_);
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

bool ImmichSource::allocate_download_() {
  if (this->encoded_ == nullptr)
    this->encoded_ = static_cast<uint8_t *>(heap_caps_malloc(photo_capacity, MALLOC_CAP_SPIRAM));
  return this->encoded_ != nullptr;
}

bool ImmichSource::has_ip_() const {
  return this->wifi_events_ != nullptr && (xEventGroupGetBits(this->wifi_events_) & got_ip) != 0;
}

int ImmichSource::fetch_(std::string_view path, std::string_view body, size_t limit, size_t &received,
                         int64_t deadline_us) {
  received = 0;
  const bool optional = deadline_us != 0;
  const auto now = esp_timer_get_time();
  if (!optional)
    deadline_us = now + 12'000'000;
  if (now >= deadline_us)
    return 0;
  std::string url = this->settings_.immich_url;
  url += path;

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

  const auto set_timeout = [&]() {
    const auto remaining = deadline_us - esp_timer_get_time();
    return remaining > 0 &&
           esp_http_client_set_timeout_ms(this->http_, std::min<int64_t>(optional ? 1'000 : 3'000,
                                                                         (remaining + 999) / 1000)) == ESP_OK;
  };
  if (esp_http_client_set_url(this->http_, url.c_str()) != ESP_OK || !set_timeout() ||
      esp_http_client_set_method(this->http_, body.empty() ? HTTP_METHOD_GET : HTTP_METHOD_POST) != ESP_OK ||
      esp_http_client_set_header(this->http_, "Content-Type", "application/json") != ESP_OK)
    return 0;
  if (esp_http_client_open(this->http_, body.size()) != ESP_OK) {
    esp_http_client_close(this->http_);
    return 0;
  }

  const int status = [&]() {
    if (!body.empty() && (!set_timeout() ||
                         esp_http_client_write(this->http_, body.data(), body.size()) != static_cast<int>(body.size())))
      return 0;
    if (!set_timeout())
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
    while (set_timeout()) {
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

std::vector<ImmichPhoto> ImmichSource::search_(std::optional<std::chrono::sys_days> local_day, int64_t deadline_us) {
  // Optional pairing work should not wait for a new Wi-Fi association.
  const bool pairing = local_day.has_value();
  if (!(pairing ? this->has_ip_() : this->online_()))
    return {};
  if (!this->allocate_download_())
    return {};

  size_t length;
  size_t count = photo_search_batch;
  auto request = random_photo_request(count, local_day);
  int status = this->fetch_("/search/random", request, search_capacity, length, deadline_us);
  if (!pairing && status == 413) {
    count = 4;
    request = random_photo_request(count);
    status = this->fetch_("/search/random", request, search_capacity, length);
  }
  if (status != 200) {
    if (!pairing)
      ESP_LOGW(tag, "Photo search failed (%d)", status);
    return {};
  }
  return parse_photo_search({reinterpret_cast<const char *>(this->encoded_), length}, count);
}

bool ImmichSource::next_asset_(ImmichPhoto &asset) {
  if (this->pending_.empty()) {
    this->pending_ = this->search_();
    if (this->pending_.empty())
      return false;
    // Prefer fresh results without stalling a library smaller than one batch.
    std::stable_partition(this->pending_.begin(), this->pending_.end(), [&](const ImmichPhoto &photo) {
      return this->history_.recently_seen(photo.id, photo_search_batch);
    });
  }
  asset = this->pending_.back();
  this->pending_.pop_back();
  return true;
}

AssetId ImmichSource::companion_for_(const ImmichPhoto &primary, int64_t deadline_us) {
  if (!primary.capture_day)
    return {};
  auto choose = [&](const std::vector<ImmichPhoto> &photos) {
    AssetId first{};
    for (const auto &candidate : photos) {
      if (!is_portrait_companion(primary, candidate))
        continue;
      if (!this->history_.recently_seen(candidate.id, photo_search_batch))
        return candidate.id;
      if (first[0] == '\0')
        first = candidate.id;
    }
    return first;
  };
  const auto known = choose(this->pending_);
  return known[0] != '\0' ? known : choose(this->search_(primary.capture_day, deadline_us));
}

ImmichSource::PreviewStatus ImmichSource::load_preview_(const AssetId &id, DecodedPreview &preview,
                                                        bool companion, int64_t deadline_us) {
  const bool optional = deadline_us != 0;
  const auto decode = [&](const uint8_t *data, size_t length) { return preview.decode(data, length, companion); };
  const auto cached = this->previews_.find(id);
  if (!cached.empty()) {
    const auto start = esp_timer_get_time();
    const auto result = decode(cached.data(), cached.size());
    if (result == DecodedPreview::Result::Ready) {
      ESP_LOGI(tag, "Preview prepared from memory, bytes=%zu decode_us=%" PRId64,
               cached.size(), esp_timer_get_time() - start);
      return PreviewStatus::Ready;
    }
    if (result == DecodedPreview::Result::NoMemory)
      return PreviewStatus::Retry;
    this->previews_.discard(id);
  }
  if (!(optional ? this->has_ip_() : this->online_()))
    return PreviewStatus::Retry;
  if (!this->allocate_download_())
    return PreviewStatus::Retry;

  const auto request_us = esp_timer_get_time();
  size_t length;
  const auto path = std::string("/assets/") + id.data() + "/thumbnail?size=preview";
  const int status = this->fetch_(path, {}, photo_capacity, length, deadline_us);
  const auto decode_us = esp_timer_get_time();
  if (status == 200) {
    const auto result = decode(this->encoded_, length);
    if (result == DecodedPreview::Result::Ready) {
      this->previews_.remember(id, {this->encoded_, length});
      ESP_LOGI(tag, "Preview prepared, bytes=%zu fetch_us=%" PRId64 " decode_us=%" PRId64,
               length, decode_us - request_us, esp_timer_get_time() - decode_us);
      return PreviewStatus::Ready;
    }
    if (result == DecodedPreview::Result::NoMemory)
      return PreviewStatus::Retry;
  }
  if (!optional)
    ESP_LOGW(tag, "Photo preview was unavailable or could not be decoded (%d)", status);
  return status == 404 || status == 410 || status == 413 || status == 200 ? PreviewStatus::Missing
                                                                      : PreviewStatus::Retry;
}

bool ImmichSource::prepare(SlideIndex slide, uint16_t *panel) {
  const bool new_position = slide == this->history_.next();
  auto *existing = this->history_.find(slide);
  if (!new_position && existing == nullptr)
    return false;
  const bool remembered = existing != nullptr && existing->primary[0] != '\0';
  ImmichPhoto seed;
  PhotoSlide frame;
  if (remembered) {
    frame = *existing;
  } else {
    if (!this->next_asset_(seed))
      return false;
    frame.primary = seed.id;
  }

  DecodedPreview primary;
  const auto status = this->load_preview_(frame.primary, primary, false);
  if (status != PreviewStatus::Ready) {
    if (!remembered && status == PreviewStatus::Retry)
      this->pending_.push_back(seed);
    if (remembered && status == PreviewStatus::Missing)
      *existing = {};
    return false;
  }

  // The first photo should appear as soon as its preview is ready.
  const auto pair_deadline_us = esp_timer_get_time() + (remembered ? 2 : 1) * pairing_time_us;
  if (!remembered && slide > 1 && this->settings_.portrait_pairing && primary.portrait())
    frame.companion = this->companion_for_(seed, pair_deadline_us);

  DecodedPreview companion;
  if (frame.companion[0] != '\0' && primary.portrait()) {
    const auto right = this->load_preview_(frame.companion, companion, true, pair_deadline_us);
    if (remembered && right == PreviewStatus::Retry)
      return false;  // Keep the existing pair together while waiting for Wi-Fi.
    if (right != PreviewStatus::Ready || !companion.portrait())
      frame.companion = {};
  } else {
    frame.companion = {};
  }

  if (!remembered && frame.companion[0] != '\0')
    std::erase_if(this->pending_, [&](const ImmichPhoto &photo) { return photo.id == frame.companion; });
  primary.render(panel, frame.companion[0] == '\0' ? nullptr : &companion);
  if (new_position)
    this->history_.append(frame);
  else
    *existing = frame;
  return true;
}

}  // namespace espframe::native
