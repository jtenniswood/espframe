#include "immich.h"

#include "preview_decoder.h"
#include "web_settings.h"

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
#include <utility>

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
  if (this->cancelled_)
    return false;
  if (!this->wifi_started_) {
    this->wifi_started_ = true;
    this->wifi_ready_ = this->start_wifi_();
    if (!this->wifi_ready_)
      ESP_LOGW(tag, "Could not initialize Guition Wi-Fi");
  }
  if (!this->wifi_ready_)
    return false;
  if (!this->has_ip_()) {
    this->http_.disconnect();
    esp_wifi_connect();
    const auto deadline = esp_timer_get_time() + 8'000'000;
    while (!this->has_ip_() && !this->cancelled_ && esp_timer_get_time() < deadline)
      xEventGroupWaitBits(this->wifi_events_, got_ip, pdFALSE, pdFALSE, pdMS_TO_TICKS(100));
    if (this->cancelled_ || !this->has_ip_())
      return false;
  }
  start_web_settings(this->settings_);
  if (this->settings_.immich_url.starts_with("https://") && std::time(nullptr) < 1'690'000'000) {
    if (!this->sntp_started_) {
      esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
      this->sntp_started_ = esp_netif_sntp_init(&config) == ESP_OK;
    }
    if (!this->sntp_started_)
      return false;
    const auto deadline = esp_timer_get_time() + 3'000'000;
    while (!this->cancelled_ && esp_timer_get_time() < deadline) {
      const auto result = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(100));
      if (result == ESP_OK)
        return true;
      if (result != ESP_ERR_TIMEOUT)
        break;
    }
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
  std::string url = this->settings_.immich_url;
  url += path;
  return this->http_.read(url, this->settings_.immich_key.c_str(), body, {this->encoded_, limit}, received, deadline_us);
}

bool ImmichSource::select_search_api_() {
  if (this->settings_.photo_source.mode == PhotoSourceMode::Library || this->search_api_ != ImmichSearchApi::Unknown)
    return true;
  size_t length;
  const int status = this->fetch_("/server/version", {}, 1024, length);
  if (status == 200)
    this->search_api_ = parse_search_api({reinterpret_cast<const char *>(this->encoded_), length});
  if (this->search_api_ == ImmichSearchApi::Unknown) {
    ESP_LOGW(tag, "Could not determine Immich search capabilities for the selected photo source (%d)", status);
    return false;
  }
  return true;
}

std::vector<ImmichPhoto> ImmichSource::search_(std::optional<std::chrono::sys_days> local_day, int64_t deadline_us) {
  // Optional pairing work should not wait for a new Wi-Fi association.
  const bool pairing = local_day.has_value();
  if (!(pairing ? this->has_ip_() : this->online_()))
    return {};
  if (!this->allocate_download_())
    return {};
  if (!pairing && !this->select_search_api_())
    return {};

  const auto &source = this->settings_.photo_source;
  const bool legacy_album = source.mode == PhotoSourceMode::Album && this->search_api_ == ImmichSearchApi::Legacy;
  uint32_t page = !pairing && legacy_album ? this->album_page_ : 1;
  size_t count = !pairing && legacy_album ? this->album_page_size_ : photo_search_batch;
  auto request = make_photo_search(count, source, this->search_api_, local_day, page);
  if (request.path.empty())
    return {};
  size_t length;
  int status = this->fetch_(request.path, request.body, search_capacity, length, deadline_us);
  if (!pairing && status == 413 && count > 4) {
    count = 4;
    if (legacy_album) {
      // A smaller page size changes the offsets. Restart the same album.
      this->album_page_size_ = count;
      this->album_page_ = 1;
      page = 1;
    }
    request = make_photo_search(count, source, this->search_api_, local_day, page);
    status = this->fetch_(request.path, request.body, search_capacity, length);
  }
  if (status != 200) {
    if (!pairing)
      ESP_LOGW(tag, "Photo search failed (%d)", status);
    return {};
  }
  auto response = parse_photo_search({reinterpret_cast<const char *>(this->encoded_), length}, count, request.paged,
                                     source.mode == PhotoSourceMode::Favorites);
  if (!pairing && request.paged && response.valid)
    this->album_page_ = response.next_page > page ? response.next_page : 1;
  return std::move(response.photos);
}

bool ImmichSource::next_asset_(ImmichPhoto &asset) {
  if (this->pending_.empty()) {
    this->pending_ = this->search_();
    if (this->pending_.empty())
      return false;
    if (this->settings_.photo_source.mode == PhotoSourceMode::Album && this->search_api_ == ImmichSearchApi::Legacy) {
      // Consume the paged album from newest to oldest across batches.
      std::reverse(this->pending_.begin(), this->pending_.end());
    } else {
      // Prefer fresh results without stalling a library smaller than one batch.
      std::stable_partition(this->pending_.begin(), this->pending_.end(), [&](const ImmichPhoto &photo) {
        return this->history_.recently_seen(photo.id, photo_search_batch);
      });
    }
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
  if (this->cancelled_)
    return PreviewStatus::Retry;
  const bool optional = deadline_us != 0;
  const auto decode = [&](const uint8_t *data, size_t length) {
    return preview.decode(data, length, companion, &this->jpeg_, this->settings_.display_mode);
  };
  const auto cached = this->previews_.find(id);
  if (!cached.empty()) {
    const auto start = esp_timer_get_time();
    const auto result = decode(cached.data(), cached.size());
    if (result == DecodedPreview::Result::Ready) {
      ESP_LOGI(tag, "Preview prepared from memory, bytes=%zu decode_us=%" PRId64 " hardware_jpeg=%d",
               cached.size(), esp_timer_get_time() - start, preview.hardware_jpeg());
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
  if (status == 200 && !this->cancelled_) {
    const auto result = decode(this->encoded_, length);
    if (result == DecodedPreview::Result::Ready) {
      this->previews_.remember(id, {this->encoded_, length});
      ESP_LOGI(tag, "Preview prepared, bytes=%zu fetch_us=%" PRId64 " decode_us=%" PRId64 " hardware_jpeg=%d",
               length, decode_us - request_us, esp_timer_get_time() - decode_us, preview.hardware_jpeg());
      return PreviewStatus::Ready;
    }
    if (result == DecodedPreview::Result::NoMemory)
      return PreviewStatus::Retry;
  }
  if (this->cancelled_)
    return PreviewStatus::Retry;
  if (!optional)
    ESP_LOGW(tag, "Photo preview was unavailable or could not be decoded (%d)", status);
  return status == 404 || status == 410 || status == 413 || status == 200 ? PreviewStatus::Missing
                                                                      : PreviewStatus::Retry;
}

bool ImmichSource::prepare(SlideIndex slide, uint16_t *panel) {
  if (this->cancelled_)
    return false;
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
  if (status != PreviewStatus::Ready || this->cancelled_) {
    if (!remembered && (status == PreviewStatus::Retry || this->cancelled_))
      this->pending_.push_back(seed);
    if (remembered && status == PreviewStatus::Missing && !this->cancelled_)
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

  if (this->cancelled_) {
    if (!remembered)
      this->pending_.push_back(seed);
    return false;
  }
  if (!remembered && frame.companion[0] != '\0')
    std::erase_if(this->pending_, [&](const ImmichPhoto &photo) { return photo.id == frame.companion; });
  primary.render(panel, frame.companion[0] == '\0' ? nullptr : &companion, this->settings_.display_mode);
  if (new_position)
    this->history_.append(frame);
  else
    *existing = frame;
  return true;
}

}  // namespace espframe::native
