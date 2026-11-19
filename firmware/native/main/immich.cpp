#include "immich.h"

#include "immich_memories.h"
#include "live_trial.h"
#include "native_network.h"
#include "photo_caption.h"
#include "preview_decoder.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cinttypes>
#include <utility>

namespace espframe::native {
namespace {

constexpr const char *tag = "immich";
constexpr size_t photo_capacity = 2 * 1024 * 1024;
constexpr size_t search_capacity = 64 * 1024;
constexpr size_t memory_capacity = 1024 * 1024;
constexpr size_t memory_pool_capacity = 3 * photo_search_batch;
constexpr int64_t memory_search_budget_us = 3'000'000;
constexpr int64_t memory_retry_us = 30'000'000;
constexpr int64_t pairing_time_us = 1'500'000;
constexpr unsigned pairing_searches_per_prepare = 4;
extern const uint8_t caption_atlas[] asm("_binary_caption_glyphs_bin_start");
extern const uint8_t caption_atlas_end[] asm("_binary_caption_glyphs_bin_end");
extern const uint8_t date_atlas[] asm("_binary_date_glyphs_bin_start");
extern const uint8_t date_atlas_end[] asm("_binary_date_glyphs_bin_end");

class TrialStageTimer {
 public:
  explicit TrialStageTimer(TrialPreparationStage stage) : stage_(stage) {
    if constexpr (SWIPE_TRIAL_IMMICH)
      started_us_ = esp_timer_get_time();
  }

  ~TrialStageTimer() {
    if constexpr (SWIPE_TRIAL_IMMICH)
      record_live_trial_preparation_time(stage_, static_cast<uint32_t>(esp_timer_get_time() - started_us_));
  }

 private:
  TrialPreparationStage stage_;
  int64_t started_us_{};
};

std::optional<std::chrono::sys_days> local_today() {
  const auto local = local_clock_time(std::time(nullptr));
  if (!local)
    return {};
  using namespace std::chrono;
  return sys_days{year(local->tm_year + 1900) / (local->tm_mon + 1) / local->tm_mday};
}

}  // namespace

bool ImmichSource::online_() {
  const auto connection = network_.photo_connection();
  if (connection != http_connection_) {
    http_.disconnect();
    prepared_http_.disconnect();
    prepared_retry_us_ = 0;
    http_connection_ = connection;
  }
  return connection != 0 && !cancelled_;
}

bool ImmichSource::allocate_download_() {
  if (this->encoded_ == nullptr)
    this->encoded_ = static_cast<uint8_t *>(heap_caps_malloc(photo_capacity, MALLOC_CAP_SPIRAM));
  return this->encoded_ != nullptr;
}

int ImmichSource::fetch_(std::string_view path, std::string_view body, size_t limit, size_t &received,
                         int64_t deadline_us, bool report_response) {
  if (!online_()) {
    received = 0;
    return 0;
  }
  std::string url = this->settings_.immich_url;
  url += path;
  const int status = this->http_.read(url, this->settings_.immich_key.c_str(), body, {this->encoded_, limit},
                                      received, deadline_us);
  if constexpr (!SWIPE_ESPHOME_TRIAL) {
    if (report_response && !cancelled_)
      this->response_ = immich_response(status);
  }
  return status;
}

bool ImmichSource::select_search_api_(PhotoSourceMode mode) {
  const bool library = mode == PhotoSourceMode::Library;
  if ((library && !this->capture_date_range_.active()) || this->search_api_ != ImmichSearchApi::Unknown)
    return true;
  size_t length;
  const int status = this->fetch_("/server/version", {}, 1024, length);
  if (status == 200)
    this->search_api_ = parse_search_api({reinterpret_cast<const char *>(this->encoded_), length});
  if (this->search_api_ == ImmichSearchApi::Unknown) {
    ESP_LOGW(tag, "Could not determine Immich search capabilities for the selected photo source (%d)", status);
    return library;
  }
  return true;
}

std::vector<ImmichPhoto> ImmichSource::search_(const PhotoSource &source,
                                              std::optional<std::chrono::sys_days> local_day, int64_t deadline_us) {
  const bool pairing = local_day.has_value();
  const bool custom = source.mode == PhotoSourceMode::Custom;
  const auto &filters = this->settings_.photo_filters;
  if (!online_())
    return {};
  if (!this->allocate_download_())
    return {};
  if (!pairing && !this->select_search_api_(source.mode)) {
    if (custom)
      this->source_status_ = PhotoSourceStatus::FiltersUnavailable;
    return {};
  }

  if (custom && !pairing) {
    const auto availability = photo_filter_status(filters, this->search_api_);
    if (availability != PhotoFilterStatus::Ready) {
      this->source_status_ = availability == PhotoFilterStatus::Incomplete ? PhotoSourceStatus::IncompleteFilters
                                                                           : PhotoSourceStatus::UnsupportedFilters;
      return {};
    }
    this->filter_branch_ = next_photo_filter_branch(filters, this->search_api_, next_filter_group_, next_filter_album_);
  }

  const bool paged = custom ? photo_filter_uses_metadata(this->filter_branch_, this->search_api_)
                            : source.mode == PhotoSourceMode::Album && this->search_api_ == ImmichSearchApi::Legacy;
  auto *album_page = !pairing && paged ? &this->album_pages_[custom ? this->filter_branch_.album_ids : source.album_id]
                                       : nullptr;
  if (!pairing)
    this->pending_is_paged_ = paged;
  uint32_t page = album_page ? album_page->page : 1;
  size_t count = album_page ? album_page->count : photo_search_batch;
  const bool with_exif = this->settings_.metadata.show_location || this->settings_.requires_portrait_pair();
  const auto dates = this->settings_.photo_source.mode == PhotoSourceMode::Memories
                         ? CaptureDateRange{} : this->capture_date_range_;
  const auto make_request = [&](bool exif) {
    const auto pairing_days = this->settings_.portrait_pairing_days;
    return custom ? make_filtered_photo_search(count, filters, this->filter_branch_, this->search_api_, local_day,
                                               page, exif, dates, pairing_days)
                  : make_photo_search(count, source, this->search_api_, local_day, page, exif, dates, pairing_days);
  };
  auto request = make_request(with_exif);
  if (request.path.empty())
    return {};
  size_t length;
  int status = this->fetch_(request.path, request.body, search_capacity, length, deadline_us,
                            !pairing || this->settings_.requires_portrait_pair());
  if (!pairing && status == 413 && count > 4) {
    count = 4;
    if (album_page) {
      // A smaller page size changes the offsets. Restart the same album.
      *album_page = {1, count};
      page = 1;
    }
    request = make_request(with_exif);
    status = this->fetch_(request.path, request.body, search_capacity, length);
  }
  if (!pairing && with_exif && (status == 400 || status == 413)) {
    request = make_request(false);
    status = this->fetch_(request.path, request.body, search_capacity, length);
  }
  if (status != 200) {
    if constexpr (SWIPE_TRIAL_IMMICH)
      record_live_trial_search(pairing, status, 0);
    if (!pairing) {
      ESP_LOGW(tag, "Photo search failed (%d)", status);
      if (custom)
        this->source_status_ = PhotoSourceStatus::FiltersUnavailable;
    }
    return {};
  }
  const std::string_view favorite_mode = custom && filters.criteria.favorites_enabled
                                            ? std::string_view(filters.criteria.favorite_mode) : "Any";
  auto response = parse_photo_search({reinterpret_cast<const char *>(this->encoded_), length}, count, request.paged,
                                     source.mode == PhotoSourceMode::Favorites || favorite_mode == "Favorites only",
                                     request.dates, favorite_mode == "Exclude favorites", this->settings_.photo_orientation);
  if constexpr (SWIPE_TRIAL_IMMICH)
    record_live_trial_search(pairing, status, response.photos.size());
  if (album_page && response.valid)
    album_page->page = response.next_page > page ? response.next_page : 1;
  if constexpr (!SWIPE_ESPHOME_TRIAL) {
    if (!pairing && !response.valid && !cancelled_)
      this->response_ = ImmichResponse::RequestFailed;
  }
  if (!pairing && response.valid && response.photos.empty() && this->settings_.photo_source.mode != PhotoSourceMode::Memories)
    this->source_status_ = custom || this->settings_.photo_orientation != PhotoOrientation::Any
                              ? PhotoSourceStatus::FiltersUnavailable : PhotoSourceStatus::NoPhotos;
  return std::move(response.photos);
}

bool ImmichSource::update_memory_day_() {
  const auto today = local_today();
  if (!today) {
    this->source_status_ = PhotoSourceStatus::WaitingForTime;
    return false;
  }
  if (this->memory_day_ != today) {
    this->memory_day_ = today;
    this->pending_.clear();
    this->memory_pool_.clear();
    this->next_memory_date_ = 0;
    this->memory_retry_at_ = 0;
    this->using_memory_fallback_ = false;
    this->memory_response_too_large_ = false;
    this->source_status_ = PhotoSourceStatus::None;
  } else if (this->using_memory_fallback_ && esp_timer_get_time() >= this->memory_retry_at_) {
    this->pending_.clear();
    this->using_memory_fallback_ = false;
  }
  return true;
}

bool ImmichSource::update_capture_dates_() {
  const auto &settings = this->settings_.capture_dates;
  const auto range = settings.resolve(settings.needs_time() ? local_today() : std::nullopt);
  if (!range) {
    this->source_status_ = PhotoSourceStatus::WaitingForTime;
    return false;
  }
  this->source_status_ = PhotoSourceStatus::None;
  if (this->capture_date_range_ != *range) {
    this->capture_date_range_ = *range;
    this->pending_.clear();
    for (auto &[album, pagination] : this->album_pages_)
      pagination.page = 1;
  }
  return true;
}

std::vector<ImmichPhoto> ImmichSource::memories_() {
  const auto &source = this->settings_.photo_source;
  const unsigned date_count = 1 + 2 * memory_window_days[source.memory_window];
  const auto start = esp_timer_get_time();
  if (start >= this->memory_retry_at_ && this->next_memory_date_ < date_count && online_() && allocate_download_()) {
    const auto deadline = start + memory_search_budget_us;
    while (this->next_memory_date_ < date_count && esp_timer_get_time() < deadline) {
      size_t length{};
      const auto path = memory_request_path(*this->memory_day_, this->next_memory_date_);
      const int status = this->fetch_(path, {}, memory_capacity, length, deadline);
      if (this->cancelled_ || (status == 0 && esp_timer_get_time() >= deadline))
        return {};
      auto results = status == 200 ? parse_memories({reinterpret_cast<const char *>(this->encoded_), length},
                                                   photo_search_batch, this->settings_.photo_orientation) : std::nullopt;
      // A single oversized date should not prevent the rest of the window
      // from producing memories. Other request failures use the retry delay.
      if (!results && status != 413) {
        ESP_LOGW(tag, "On This Day request failed (%d). Check the Immich connection and memory.read permission.", status);
        this->memory_retry_at_ = esp_timer_get_time() + memory_retry_us;
        this->source_status_ = PhotoSourceStatus::MemoriesUnavailable;
        break;
      }
      this->memory_response_too_large_ |= status == 413;
      ++this->next_memory_date_;
      if (!results || results->empty())
        continue;
      this->source_status_ = PhotoSourceStatus::None;
      for (const auto &photo : *results) {
        if (this->memory_pool_.size() == memory_pool_capacity)
          break;
        if (std::none_of(this->memory_pool_.begin(), this->memory_pool_.end(),
                         [&](const ImmichPhoto &other) { return other.id == photo.id; }))
          this->memory_pool_.push_back(photo);
      }
      return std::move(*results);
    }
  }
  if (this->next_memory_date_ == date_count && this->memory_pool_.empty()) {
    if (!this->memory_response_too_large_)
      ESP_LOGI(tag, "No On This Day photos in the configured window");
    this->next_memory_date_ = 0;
    this->memory_retry_at_ = esp_timer_get_time() + memory_retry_us;
    this->source_status_ = this->memory_response_too_large_ ? PhotoSourceStatus::MemoriesUnavailable : PhotoSourceStatus::MemoriesEmpty;
    this->memory_response_too_large_ = false;
  }
  if (!this->memory_pool_.empty())
    return this->memory_pool_;
  this->using_memory_fallback_ = source.memory_fallback && esp_timer_get_time() < this->memory_retry_at_;
  auto fallback = this->using_memory_fallback_ ? this->search_(PhotoSource{}) : std::vector<ImmichPhoto>{};
  if (!fallback.empty())
    this->source_status_ = PhotoSourceStatus::None;
  return fallback;
}

bool ImmichSource::next_asset_(ImmichPhoto &asset) {
  const auto &source = this->settings_.photo_source;
  const bool memory_mode = source.mode == PhotoSourceMode::Memories;
  if (!(memory_mode ? this->update_memory_day_() : this->update_capture_dates_()))
    return false;
  if (this->pending_.empty()) {
    this->pending_ = memory_mode ? this->memories_() : this->search_(source);
    if (this->settings_.requires_portrait_pair()) {
      const auto removed = std::erase_if(this->pending_, [](const ImmichPhoto &photo) { return !can_start_portrait_pair(photo); });
      if (removed != 0 && this->pending_.empty())
        this->source_status_ = PhotoSourceStatus::WaitingForPair;
    }
    // A prepared slide can still work when the direct preview is unavailable.
    if (!this->settings_.has_prepared_slides()) {
      const auto now = esp_timer_get_time();
      std::erase_if(this->pending_, [&](const ImmichPhoto &photo) {
        return this->unavailable_.contains(photo.id, now);
      });
    }
    if (this->pending_.empty())
      return false;
    if (this->pending_is_paged_) {
      // Consume the paged album from newest to oldest across batches.
      std::reverse(this->pending_.begin(), this->pending_.end());
    } else {
      // Prefer fresh results without stalling a library smaller than one batch.
      std::stable_partition(this->pending_.begin(), this->pending_.end(), [&](const ImmichPhoto &photo) {
        return this->history_.recently_seen(photo.id, photo_search_batch);
      });
    }
  }
  return this->take_pending_(asset);
}

bool ImmichSource::take_pending_(ImmichPhoto &asset) {
  if (this->pending_.empty())
    return false;
  if (this->settings_.requires_portrait_pair() && !this->pending_is_paged_) {
    if (const auto match = find_pair_seed(this->pending_, this->settings_.portrait_pairing_days)) {
      const auto seed = this->pending_.begin() + *match;
      asset = *seed;
      this->pending_.erase(seed);
      return true;
    }
  }
  asset = this->pending_.back();
  this->pending_.pop_back();
  return true;
}

bool ImmichSource::select_pair_(ImmichPhoto &seed, AssetId &companion) {
  const auto deadline = esp_timer_get_time() + pairing_time_us;
  for (unsigned search = 1;; ++search) {
    companion = this->companion_for_(seed, deadline);
    if (this->cancelled_) {
      this->pending_.push_back(seed);
      return false;
    }
    if (companion[0] != '\0')
      return true;
    if (search == pairing_searches_per_prepare || esp_timer_get_time() >= deadline || !this->take_pending_(seed))
      return false;
  }
}

AssetId ImmichSource::companion_for_(const ImmichPhoto &primary, int64_t deadline_us) {
  if (!primary.metadata.capture_day)
    return {};
  auto choose = [&](const std::vector<ImmichPhoto> &photos) {
    AssetId best{};
    auto closest = std::chrono::days::max();
    bool best_seen = true;
    const auto now = esp_timer_get_time();
    for (const auto &candidate : photos) {
      if (!is_portrait_companion(primary, candidate, this->settings_.portrait_pairing_days))
        continue;
      if (!this->settings_.has_prepared_slides() && this->unavailable_.contains(candidate.id, now))
        continue;
      const bool seen = this->history_.recently_seen(candidate.id, photo_search_batch);
      const auto distance = std::chrono::abs(*candidate.metadata.capture_day - *primary.metadata.capture_day);
      if ((!seen && best_seen) || (seen == best_seen && distance < closest)) {
        best = candidate.id;
        best_seen = seen;
        closest = distance;
      }
    }
    return best;
  };
  const auto known = choose(this->pending_);
  if (known[0] != '\0')
    return known;
  const auto &source = this->settings_.photo_source;
  if (source.mode == PhotoSourceMode::Memories) {
    if (!this->using_memory_fallback_)
      return choose(this->memory_pool_);
    return choose(this->search_(PhotoSource{}, primary.metadata.capture_day, deadline_us));
  }
  return choose(this->search_(source, primary.metadata.capture_day, deadline_us));
}

void ImmichSource::discard_candidate_(const AssetId &id) {
  const auto matches = [&](const ImmichPhoto &photo) { return photo.id == id; };
  std::erase_if(this->pending_, matches);
  std::erase_if(this->memory_pool_, matches);
}

bool ImmichSource::wait_for_pixel_work_() const {
  if (!this->pixel_work_deferred_.load(std::memory_order_relaxed) || this->cancelled_)
    return !this->cancelled_;

  const auto started = esp_timer_get_time();
  do {
    vTaskDelay(pdMS_TO_TICKS(4));
  } while (this->pixel_work_deferred_.load(std::memory_order_relaxed) && !this->cancelled_);
  if constexpr (SWIPE_TRIAL_IMMICH)
    record_live_trial_pixel_pause(static_cast<uint32_t>((esp_timer_get_time() - started) / 1000));
  return !this->cancelled_;
}

ImmichSource::PreviewStatus ImmichSource::load_preview_(const AssetId &id, DecodedPreview &preview,
                                                        bool for_pair, int64_t deadline_us, bool report_response) {
  if (this->cancelled_)
    return PreviewStatus::Retry;
  const bool optional = deadline_us != 0;
  const auto orientation = for_pair ? PhotoOrientation::Portrait : this->settings_.photo_orientation;
  const auto framing = for_pair ? this->settings_.pair_display_mode() : this->settings_.display_mode;
  const auto decode = [&](const uint8_t *data, size_t length) {
    int64_t started_us = 0;
    if constexpr (SWIPE_TRIAL_IMMICH)
      started_us = esp_timer_get_time();
    const auto result = preview.decode(data, length, for_pair, &this->jpeg_, framing, this->checkpoint_, orientation);
    if constexpr (SWIPE_TRIAL_IMMICH) {
      const auto elapsed = static_cast<uint32_t>(esp_timer_get_time() - started_us);
      auto stage = TrialPreparationStage::RejectedPreview;
      if (result == DecodedPreview::Result::Ready) {
        if (preview.hardware_jpeg())
          stage = TrialPreparationStage::HardwareJpeg;
        else if (is_webp_preview(data, length))
          stage = TrialPreparationStage::Webp;
        else
          stage = TrialPreparationStage::SoftwareJpeg;
      }
      record_live_trial_preparation_time(stage, elapsed);
    }
    return result;
  };
  const auto cached = this->previews_.find(id);
  if (!cached.empty()) {
    const auto start = esp_timer_get_time();
    const auto result = decode(cached.data(), cached.size());
    if (result == DecodedPreview::Result::Ready) {
      this->previews_.remember(id, cached);
      ESP_LOGI(tag, "Preview prepared from memory, bytes=%zu decode_us=%" PRId64 " hardware_jpeg=%d",
               cached.size(), esp_timer_get_time() - start, preview.hardware_jpeg());
      return PreviewStatus::Ready;
    }
    if (result == DecodedPreview::Result::NoMemory || result == DecodedPreview::Result::Interrupted)
      return PreviewStatus::Retry;
    if (result == DecodedPreview::Result::OrientationMismatch)
      return PreviewStatus::Missing;
    this->previews_.discard(id);
  }
  if (!online_())
    return PreviewStatus::Retry;
  if (this->unavailable_.contains(id, esp_timer_get_time()))
    return PreviewStatus::Missing;
  if (!this->allocate_download_())
    return PreviewStatus::Retry;

  const auto request_us = esp_timer_get_time();
  size_t length = 0;
  const auto path = std::string("/assets/") + id.data() + "/thumbnail?size=preview";
  const int status = this->fetch_(path, {}, photo_capacity, length, deadline_us, report_response);
  const auto decode_us = esp_timer_get_time();
  if constexpr (SWIPE_TRIAL_IMMICH) {
    record_live_trial_preview(status);
    record_live_trial_preparation_time(TrialPreparationStage::Transfer,
                                       static_cast<uint32_t>(decode_us - request_us), length);
  }
  if (status == 200) {
    const std::span<const uint8_t> bytes{this->encoded_, length};
    const auto result = decode(this->encoded_, length);
    if (result == DecodedPreview::Result::Ready) {
      if (!this->checkpoint_()) {
        this->previews_.remember_interrupted(id, bytes);
        return PreviewStatus::Retry;
      }
      this->previews_.remember(id, bytes);
      ESP_LOGI(tag, "Preview prepared, bytes=%zu fetch_us=%" PRId64 " decode_us=%" PRId64 " hardware_jpeg=%d",
               length, decode_us - request_us, esp_timer_get_time() - decode_us, preview.hardware_jpeg());
      return PreviewStatus::Ready;
    }
    if (result == DecodedPreview::Result::Interrupted) {
      if (is_jpeg_preview(bytes.data(), bytes.size()) || is_webp_preview(bytes.data(), bytes.size()))
        this->previews_.remember_interrupted(id, bytes);
      return PreviewStatus::Retry;
    }
    if (result == DecodedPreview::Result::NoMemory)
      return PreviewStatus::Retry;
    if (result == DecodedPreview::Result::OrientationMismatch)
      return PreviewStatus::Missing;
  }
  if (this->cancelled_)
    return PreviewStatus::Retry;
  if (!optional)
    ESP_LOGW(tag, "Photo preview was unavailable or could not be decoded (%d)", status);
  return this->unavailable_.remember_failure(id, status, esp_timer_get_time()) ? PreviewStatus::Missing
                                                                              : PreviewStatus::Retry;
}

ImmichSource::PreparedPreview ImmichSource::render_prepared_(const PhotoSlide &frame, uint16_t *panel) {
  // The source's settings stay fixed while it owns the cache. The ordered
  // photo IDs identify the clean JPEG, including when a lease is cancelled
  // before its photos have been assigned a history position.
  const RecentPreviews::PreparedKey key{frame.primary, frame.companion};
  const auto cached = previews_.find(key);
  if (!cached.empty()) {
    const auto started = esp_timer_get_time();
    if (jpeg_.decode_panel(cached.data(), cached.size(), panel, checkpoint_)) {
      if constexpr (SWIPE_TRIAL_IMMICH)
        record_live_trial_preparation_time(TrialPreparationStage::PreparedCacheDecode,
                                           static_cast<uint32_t>(esp_timer_get_time() - started), cached.size());
      previews_.remember_prepared(key, cached);
      return {true, false};
    }
    if (cancelled_)
      return {};
    previews_.discard(key);
  }
  if (!online_() || esp_timer_get_time() < prepared_retry_us_ || !allocate_download_())
    return {};
  std::string url(settings_.prepared_slide_url);
  url += "/v1/slides/";
  url += frame.primary.data();
  const bool paired = frame.companion[0] != '\0';
  const auto framing = paired ? settings_.pair_display_mode() : settings_.display_mode;
  url += framing == DisplayMode::Fill ? "?mode=fill" : "?mode=fit";
  if (paired) {
    url += "&companion=";
    url += frame.companion.data();
  }
  url += "&rotation=";
  url += screen_rotation_name(settings_.screen_rotation);
  const auto orientation = this->settings_.photo_orientation;
  if (orientation != PhotoOrientation::Any)
    url += orientation == PhotoOrientation::Portrait ? "&orientation=portrait" : "&orientation=landscape";
  const auto authorization = "Bearer " + settings_.prepared_slide_token;
  const auto start = esp_timer_get_time();
  size_t length = 0;
  const int status = prepared_http_.read(url, authorization.c_str(), {}, {encoded_, photo_capacity}, length,
                                         start + 1'500'000);
  const auto decode_start = esp_timer_get_time();
  if constexpr (SWIPE_TRIAL_IMMICH)
    record_live_trial_preparation_time(TrialPreparationStage::PreparedTransfer,
                                       static_cast<uint32_t>(decode_start - start), length);
  // Avoid paying the failed request on every slide. A missing asset or an
  // unsupported image affects only that slide, so its successor can still use the service.
  // Older service versions reject the orientation option with HTTP 400.
  if (!cancelled_ && (status == 0 || status == 401 || status == 403 || status == 429 || status >= 500 ||
                      (status == 400 && orientation != PhotoOrientation::Any)))
    prepared_retry_us_ = decode_start + 30'000'000;
  if (!cancelled_ && status == 409 && orientation != PhotoOrientation::Any)
    return {false, true};
  if (status != 200)
    return {};
  const std::span<const uint8_t> bytes{encoded_, length};
  if (!cancelled_ && jpeg_.decode_panel(bytes.data(), bytes.size(), panel, checkpoint_)) {
    if constexpr (SWIPE_TRIAL_IMMICH)
      record_live_trial_preparation_time(TrialPreparationStage::PreparedDecode,
                                         static_cast<uint32_t>(esp_timer_get_time() - decode_start));
    if (checkpoint_()) {
      previews_.remember_prepared(key, bytes);
      return {true, false};
    }
  }
  if (cancelled_ && is_jpeg_preview(bytes.data(), bytes.size()))
    previews_.remember_interrupted(key, bytes);
  return {};
}

ImmichSource::PrepareResult ImmichSource::prepare(SlideIndex slide, uint16_t *panel) {
  const TrialStageTimer attempt{TrialPreparationStage::Attempt};
  if constexpr (!SWIPE_ESPHOME_TRIAL)
    this->response_ = ImmichResponse::Pending;
  if (this->cancelled_)
    return PrepareResult::Retry;
  const bool new_position = slide == this->history_.next();
  auto *existing = this->history_.find(slide);
  if (!new_position && existing == nullptr)
    return PrepareResult::Retry;
  const bool remembered = existing != nullptr && existing->primary[0] != '\0';
  const bool pairs_only = this->settings_.requires_portrait_pair();
  ImmichPhoto seed;
  PhotoSlide frame;
  if (remembered) {
    frame = *existing;
  } else {
    const TrialStageTimer selection{TrialPreparationStage::Selection};
    if (!this->next_asset_(seed))
      return PrepareResult::Retry;
    if (pairs_only && !this->select_pair_(seed, frame.companion)) {
      if (!cancelled_)
        this->source_status_ = PhotoSourceStatus::WaitingForPair;
      return PrepareResult::Retry;
    }
    frame.primary = seed.id;
    frame.metadata = seed.metadata;
  }

  const auto reject_candidate = [&](const AssetId &id) {
    this->discard_candidate_(id);
    if (remembered)
      *existing = {};
    return this->pending_.empty() ? PrepareResult::Retry : PrepareResult::NextCandidate;
  };
  const auto retry_frame = [&] {
    if (!remembered)
      this->pending_.push_back(seed);
    return PrepareResult::Retry;
  };
  bool prepared = false;
  const bool optional_pair = !remembered && !pairs_only && slide > 1 && settings_.pairs_portraits();
  bool selected_companion = false;
  const bool pair_needs_selection = optional_pair && seed.portrait;
  if (settings_.has_prepared_slides() && (!optional_pair || seed.orientation_known) &&
      (!pair_needs_selection || esp_timer_get_time() >= prepared_retry_us_)) {
    if (pair_needs_selection) {
      const TrialStageTimer selection{TrialPreparationStage::Selection};
      frame.companion = companion_for_(seed, esp_timer_get_time() + pairing_time_us);
      selected_companion = true;
    }
    const auto preview = this->render_prepared_(frame, panel);
    if (preview.orientation_mismatch)
      return reject_candidate(frame.primary);
    prepared = preview.ready;
  }
  if (this->cancelled_)
    return retry_frame();
  std::optional<TrialStageTimer> composition;
  if (!prepared) {
    DecodedPreview primary;
    const auto status = this->load_preview_(frame.primary, primary, pairs_only, 0, true);
    if (this->cancelled_ || status == PreviewStatus::Retry)
      return retry_frame();
    if (status == PreviewStatus::Missing || (pairs_only && !primary.portrait()))
      return reject_candidate(frame.primary);

    // Optional pairing leaves the first preview on the fast path.
    const auto pair_deadline_us = pairs_only ? 0 : esp_timer_get_time() + (remembered ? 2 : 1) * pairing_time_us;
    if (optional_pair && !selected_companion && primary.portrait()) {
      const TrialStageTimer selection{TrialPreparationStage::Selection};
      frame.companion = this->companion_for_(seed, pair_deadline_us);
    }

    DecodedPreview companion;
    if (frame.companion[0] != '\0' && primary.portrait()) {
      const auto right = this->load_preview_(frame.companion, companion, true, pair_deadline_us, pairs_only);
      if (right == PreviewStatus::Retry && (remembered || pairs_only))
        return retry_frame();
      if (pairs_only && (right == PreviewStatus::Missing || !companion.portrait())) {
        if (!remembered && !settings_.has_prepared_slides() && unavailable_.contains(frame.companion, esp_timer_get_time()))
          this->pending_.push_back(seed);
        return reject_candidate(frame.companion);
      }
      if (right != PreviewStatus::Ready || !companion.portrait())
        frame.companion = {};
    } else {
      frame.companion = {};
    }

    composition.emplace(TrialPreparationStage::Composition);
    if (!primary.render(panel, frame.companion[0] == '\0' ? nullptr : &companion, this->settings_.pair_display_mode(),
                        this->settings_.screen_rotation, this->checkpoint_))
      return retry_frame();
  } else {
    composition.emplace(TrialPreparationStage::Composition);
  }
  const auto &captions = this->settings_.metadata;
  const auto &tone = this->settings_.tone;
  const auto local = captions.needs_time() || tone.night_enabled ? local_clock_time(std::time(nullptr)) : std::nullopt;
  if (tone.night_enabled && !this->tone_daylight_)
    this->tone_daylight_.emplace(find_clock_zone(this->settings_.clock.timezone));
  const float night_progress = this->tone_daylight_ ? this->tone_daylight_->night_warmth(local) : 0.0f;
  if (!tint_photo({panel, pixel_count}, tone.warmth(night_progress), this->checkpoint_) || !this->checkpoint_())
    return retry_frame();
  paint_photo_caption(panel, {caption_atlas, static_cast<size_t>(caption_atlas_end - caption_atlas)},
                      {date_atlas, static_cast<size_t>(date_atlas_end - date_atlas)}, frame.metadata, captions,
                      this->settings_.screen_rotation, local);
  if (this->cancelled_)
    return retry_frame();
  if (!remembered && frame.companion[0] != '\0')
    std::erase_if(this->pending_, [&](const ImmichPhoto &photo) { return photo.id == frame.companion; });
  if (new_position)
    this->history_.append(frame);
  else
    *existing = frame;
  return PrepareResult::Ready;
}

}  // namespace espframe::native
