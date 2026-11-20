#include "immich.h"

#include "immich_memories.h"
#include "live_trial.h"
#include "native_network.h"
#include "photo_caption.h"
#include "preview_decoder.h"
#include "trial_replay.h"

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

void ImmichSource::apply_photo_settings(PhotoFeedSettings feed, std::optional<SlideIndex> first_slide) {
  this->prefetch_.cancel();
  if (this->feed_().photo_orientation != feed.photo_orientation) {
    this->prepared_retry_us_ = 0;
    this->prepared_frame_fill_retry_us_ = 0;
  }
  this->selection_ = std::move(feed);
  if (!first_slide)
    return;
  this->history_.start_at(*first_slide);
  this->pending_.clear();
  this->pending_is_paged_ = this->pending_has_next_page_ = false;
  this->album_pages_.clear();
  this->filter_branch_ = {};
  this->next_filter_group_ = this->next_filter_album_ = 0;
  this->memory_day_.reset();
  this->memory_pool_.clear();
  this->memory_retry_at_ = 0;
  this->next_memory_date_ = 0;
  this->using_memory_fallback_ = this->memory_response_too_large_ = false;
  this->source_status_ = PhotoSourceStatus::None;
}

bool ImmichSource::online_() {
  const auto connection = network_.photo_connection();
  if (connection != http_connection_) {
    http_.disconnect();
    prepared_http_.disconnect();
    prefetch_.cancel();
    prepared_retry_us_ = 0;
    prepared_frame_fill_retry_us_ = 0;
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
  const auto &filters = this->feed_().photo_filters;
  if (!pairing)
    this->pending_has_next_page_ = false;
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
  const bool with_exif = this->feed_().metadata.show_location || this->feed_().requires_portrait_pair();
  const auto dates = this->feed_().photo_source.mode == PhotoSourceMode::Memories
                         ? CaptureDateRange{} : this->capture_date_range_;
  const auto make_request = [&](bool exif) {
    const auto pairing_days = this->feed_().portrait_pairing_days;
    return custom ? make_filtered_photo_search(count, filters, this->filter_branch_, this->search_api_, local_day,
                                               page, exif, dates, pairing_days)
                  : make_photo_search(count, source, this->search_api_, local_day, page, exif, dates, pairing_days);
  };
  auto request = make_request(with_exif);
  if (request.path.empty())
    return {};
  size_t length;
  int status = this->fetch_(request.path, request.body, search_capacity, length, deadline_us,
                            !pairing || this->feed_().requires_portrait_pair());
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
                                     request.dates, favorite_mode == "Exclude favorites", this->feed_().photo_orientation);
  if constexpr (SWIPE_TRIAL_IMMICH)
    record_live_trial_search(pairing, status, response.photos.size());
  if (album_page && response.valid) {
    // A locally filtered page can be empty even when the album continues.
    this->pending_has_next_page_ = response.has_next_page(page);
    album_page->page = this->pending_has_next_page_ ? response.next_page : 1;
  }
  if constexpr (!SWIPE_ESPHOME_TRIAL) {
    if (!pairing && !response.valid && !cancelled_)
      this->response_ = ImmichResponse::RequestFailed;
  }
  if (!pairing && response.valid && response.photos.empty() && !this->pending_has_next_page_ &&
      this->feed_().photo_source.mode != PhotoSourceMode::Memories)
    this->source_status_ = custom || this->feed_().photo_orientation != PhotoOrientation::Any
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
  const auto &settings = this->feed_().capture_dates;
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
  const auto &source = this->feed_().photo_source;
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
                                                   photo_search_batch, this->feed_().photo_orientation) : std::nullopt;
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
  const auto &source = this->feed_().photo_source;
  const bool memory_mode = source.mode == PhotoSourceMode::Memories;
  if (!(memory_mode ? this->update_memory_day_() : this->update_capture_dates_())) {
    this->pending_has_next_page_ = false;
    return false;
  }
  if (this->pending_.empty()) {
    this->pending_ = memory_mode ? this->memories_() : this->search_(source);
    if (this->feed_().requires_portrait_pair()) {
      const auto removed = std::erase_if(this->pending_, [&](const ImmichPhoto &photo) {
        return !can_play_with_required_pairs(photo, this->feed_().photo_orientation);
      });
      if (removed != 0 && this->pending_.empty() && !this->pending_has_next_page_)
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
  const auto index = this->pending_index_();
  if (!index)
    return false;
  const auto seed = this->pending_.begin() + *index;
  asset = *seed;
  this->pending_.erase(seed);
  return true;
}

std::optional<size_t> ImmichSource::pending_index_(const AssetId &exclude) const {
  if (this->feed_().requires_portrait_pair() && !this->pending_is_paged_) {
    if (const auto match = find_pair_or_landscape_seed(this->pending_, this->feed_().portrait_pairing_days, exclude,
                                                       this->feed_().photo_orientation))
      return match;
  }
  for (auto i = this->pending_.size(); i > 0; --i) {
    if (this->pending_[i - 1].id != exclude)
      return i - 1;
  }
  return {};
}

void ImmichSource::prefetch_next_(const AssetId &id) {
  if constexpr (SWIPE_TRIAL_REPLAY_DISABLE_LOOKAHEAD)
    return;
  if (id[0] == '\0' || !this->online_() || this->unavailable_.contains(id, esp_timer_get_time()) ||
      !this->previews_.find(id).empty())
    return;
  this->prefetch_.start(id, this->http_connection_, this->settings_.immich_url, this->settings_.immich_key);
}

bool ImmichSource::select_pair_or_landscape_(ImmichPhoto &seed, AssetId &companion) {
  const auto deadline = esp_timer_get_time() + pairing_time_us;
  for (unsigned search = 1;; ++search) {
    if (!seed.orientation_known || !seed.portrait)
      return true;
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

AssetId ImmichSource::companion_for_(const ImmichPhoto &primary, std::optional<int64_t> search_deadline) {
  if (!primary.metadata.capture_day)
    return {};
  auto choose = [&](const std::vector<ImmichPhoto> &photos) {
    AssetId best{};
    auto closest = std::chrono::days::max();
    bool best_seen = true;
    const auto now = esp_timer_get_time();
    for (const auto &candidate : photos) {
      if (!is_portrait_companion(primary, candidate, this->feed_().portrait_pairing_days))
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
  const auto &source = this->feed_().photo_source;
  const bool memories = source.mode == PhotoSourceMode::Memories;
  if (memories && !this->using_memory_fallback_)
    return choose(this->memory_pool_);
  if (!search_deadline)
    return {};
  const PhotoSource all_photos;
  return choose(this->search_(memories ? all_photos : source, primary.metadata.capture_day, *search_deadline));
}

void ImmichSource::discard_candidate_(const AssetId &id) {
  const auto matches = [&](const ImmichPhoto &photo) { return photo.id == id; };
  std::erase_if(this->pending_, matches);
  std::erase_if(this->memory_pool_, matches);
}

bool ImmichSource::needs_caption_details_(const PhotoMetadata &metadata) const {
  const auto &captions = this->feed_().metadata;
  return !metadata.location_known && (captions.show_location || (captions.show_date && !metadata.capture_day));
}

void ImmichSource::complete_caption_details_(PhotoSlide &frame) {
  if (!this->needs_caption_details_(frame.metadata) || !online_() || !allocate_download_())
    return;
  // Search responses can omit EXIF. Keep detail requests bounded so the
  // displayed photo and its available caption can remain on screen.
  size_t length = 0;
  const auto path = std::string("/assets/") + frame.primary.data();
  if (this->fetch_(path, {}, search_capacity, length, esp_timer_get_time() + 350'000, false) != 200)
    return;
  const auto photo = parse_photo_asset({reinterpret_cast<const char *>(this->encoded_), length});
  if (!photo || photo->id != frame.primary)
    return;
  frame.metadata.city = photo->metadata.city;
  frame.metadata.country = photo->metadata.country;
  frame.metadata.location_known = true;
  if (!frame.metadata.capture_day)
    frame.metadata.capture_day = photo->metadata.capture_day;
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
                                                        PreviewTarget target, int64_t deadline_us, bool report_response,
                                                        const AssetId &lookahead) {
  if (this->cancelled_)
    return PreviewStatus::Retry;
  const bool optional = deadline_us != 0;
  const bool for_pair = target == PreviewTarget::PairPane;
  const auto orientation = for_pair ? PhotoOrientation::Portrait : this->feed_().photo_orientation;
  const PreviewLayout layout{target, this->feed_().display_mode, this->feed_().pair_display_mode()};
  const auto decode = [&](const uint8_t *data, size_t length) {
    if (is_jpeg_preview(data, length) || is_webp_preview(data, length))
      this->prefetch_next_(lookahead);
    int64_t started_us = 0;
    if constexpr (SWIPE_TRIAL_IMMICH)
      started_us = esp_timer_get_time();
    const auto result = preview.decode(data, length, layout, &this->jpeg_, this->checkpoint_, orientation);
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
    if (result == DecodedPreview::Result::NoMemory)
      this->prefetch_.release_on_memory_pressure();
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
  size_t length = !prefetch_portrait_companion && optional
                      ? 0 : this->prefetch_.take(id, this->http_connection_, {this->encoded_, photo_capacity}, deadline_us);
  int status = 200;
  if (length != 0) {
    if constexpr (SWIPE_TRIAL_IMMICH)
      record_live_trial_preparation_time(TrialPreparationStage::PrefetchWait,
                                         static_cast<uint32_t>(esp_timer_get_time() - request_us));
    if constexpr (!SWIPE_ESPHOME_TRIAL) {
      if (report_response && !cancelled_)
        this->response_ = immich_response(status);
    }
  } else {
    const auto path = std::string("/assets/") + id.data() + "/thumbnail?size=preview";
    status = this->fetch_(path, {}, photo_capacity, length, deadline_us, report_response);
    if constexpr (SWIPE_TRIAL_IMMICH) {
      record_live_trial_preview(status);
      record_live_trial_preparation_time(TrialPreparationStage::Transfer,
                                         static_cast<uint32_t>(esp_timer_get_time() - request_us), length);
    }
  }
  const auto decode_us = esp_timer_get_time();
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

ImmichSource::PreparedPreview ImmichSource::render_prepared_(const PhotoSlide &frame, uint16_t *panel,
                                                            PhotoOrientation orientation) {
  const bool paired = frame.companion[0] != '\0';
  const auto framing = paired ? this->feed_().pair_display_mode() : this->feed_().display_mode;
  const bool frame_fill = !paired && framing == DisplayMode::Fill && orientation != PhotoOrientation::Landscape;
  const RecentPreviews::PreparedKey key{frame.primary, frame.companion, framing, orientation};
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
  if (!online_())
    return {};
  const auto now = esp_timer_get_time();
  if (now < prepared_retry_us_ || (frame_fill && now < prepared_frame_fill_retry_us_) ||
      (settings_.prepared_slides_need_time() && !clock_time_is_valid(std::time(nullptr))) || !allocate_download_())
    return {};
  std::string url(settings_.prepared_slide_url);
  url += "/v1/slides/";
  url += frame.primary.data();
  url += frame_fill ? "?mode=fill-frame" : framing == DisplayMode::Fill ? "?mode=fill" : "?mode=fit";
  if (paired) {
    url += "&companion=";
    url += frame.companion.data();
  }
  url += "&rotation=";
  url += screen_rotation_name(settings_.screen_rotation);
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
  if (!cancelled_ && status == 400 && frame_fill) {
    // Older services reject the full-frame crop option. Keep their paired
    // and Fit requests available while preparing these slides locally.
    prepared_frame_fill_retry_us_ = decode_start + 30'000'000;
    return {};
  }
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

ImmichSource::PrepareResult ImmichSource::prepare(SlideIndex slide, uint16_t *panel, bool replacement,
                                                  PendingCaption &pending_caption, bool caption_only) {
  const TrialStageTimer attempt{TrialPreparationStage::Attempt};
  if constexpr (!SWIPE_ESPHOME_TRIAL)
    this->response_ = ImmichResponse::Pending;
  if (this->cancelled_)
    return PrepareResult::Retry;
  const bool new_position = slide == this->history_.next();
  auto *existing = this->history_.find(slide);
  const bool remembered = existing != nullptr && existing->primary[0] != '\0';
  if (replacement && !remembered)
    return PrepareResult::NextCandidate;
  if (!new_position && existing == nullptr)
    return PrepareResult::Retry;
  const bool reframing_pair = replacement && existing->companion[0] != '\0';
  const bool pairs_only = this->feed_().requires_portrait_pair();
  const bool preserve_pair = pairs_only || (caption_only && reframing_pair);
  ImmichPhoto seed;
  PhotoSlide frame;
  if (remembered) {
    frame = *existing;
  } else {
    const TrialStageTimer selection{TrialPreparationStage::Selection};
    if (!this->next_asset_(seed))
      return !cancelled_ && this->pending_has_next_page_ ? PrepareResult::NextCandidate : PrepareResult::Retry;
    if (pairs_only && !this->select_pair_or_landscape_(seed, frame.companion)) {
      if (!cancelled_)
        this->source_status_ = PhotoSourceStatus::WaitingForPair;
      return !cancelled_ && (!this->pending_.empty() || this->pending_has_next_page_)
                 ? PrepareResult::NextCandidate : PrepareResult::Retry;
    }
    frame.primary = seed.id;
    frame.metadata = seed.metadata;
  }

  const auto &captions = this->feed_().metadata;
  if (caption_only && this->needs_caption_details_(frame.metadata)) {
    this->complete_caption_details_(frame);
    if (this->cancelled_)
      return PrepareResult::Retry;
    if (!frame.metadata.location_known)
      return PrepareResult::NextCandidate;
    const auto &previous = existing->metadata;
    const bool changed_location = captions.show_location &&
                                  (frame.metadata.city != previous.city || frame.metadata.country != previous.country);
    const bool changed_date = captions.show_date && frame.metadata.capture_day != previous.capture_day;
    existing->metadata = frame.metadata;
    if (!changed_location && !changed_date)
      return PrepareResult::NextCandidate;
  }

  const auto reject_candidate = [&](const AssetId &id) {
    this->prefetch_.cancel();
    if (replacement)
      return PrepareResult::NextCandidate;
    this->discard_candidate_(id);
    if (remembered)
      *existing = {};
    const bool more_candidates = !this->pending_.empty() || this->pending_has_next_page_;
    return !cancelled_ && more_candidates ? PrepareResult::NextCandidate : PrepareResult::Retry;
  };
  const auto retry_frame = [&] {
    this->prefetch_.cancel_incomplete();
    if (!remembered)
      this->pending_.push_back(seed);
    return PrepareResult::Retry;
  };
  bool prepared = false;
  const bool optional_pair = !remembered && !pairs_only && slide > 1 && feed_().pairs_portraits();
  bool selected_companion = false;
  const bool pair_needs_selection = optional_pair && seed.portrait;
  // Ask the service to verify a single landscape before using an opaque
  // prepared slide in a feed that must never show a single portrait.
  const bool verify_landscape = pairs_only && frame.companion[0] == '\0';
  const bool may_prepare_landscape = feed_().photo_orientation == PhotoOrientation::Any &&
                                    (remembered || (seed.orientation_known && !seed.portrait));
  if (settings_.has_prepared_slides() && (!optional_pair || seed.orientation_known) &&
      (!verify_landscape || may_prepare_landscape) &&
      (!pair_needs_selection || esp_timer_get_time() >= prepared_retry_us_)) {
    if (pair_needs_selection) {
      const TrialStageTimer selection{TrialPreparationStage::Selection};
      frame.companion = companion_for_(seed, esp_timer_get_time() + pairing_time_us);
      selected_companion = true;
    }
    const auto prepared_orientation = verify_landscape ? PhotoOrientation::Landscape : feed_().photo_orientation;
    const auto preview = this->render_prepared_(frame, panel, prepared_orientation);
    if (preview.orientation_mismatch && !verify_landscape)
      return reject_candidate(frame.primary);
    prepared = preview.ready;
  }
  if (this->cancelled_)
    return retry_frame();
  std::optional<TrialStageTimer> composition;
  if (!prepared) {
    const auto upcoming = [&](const AssetId &exclude) {
      if (!SWIPE_TRIAL_REPLAY_DISABLE_LOOKAHEAD && !remembered && !settings_.has_prepared_slides()) {
        if (const auto index = this->pending_index_(exclude))
          return this->pending_[*index].id;
      }
      return AssetId{};
    };
    DecodedPreview primary;
    // Start a known companion during primary decode. A new Immich search
    // still waits for the decoded portrait so it cannot delay a landscape.
    if (prefetch_portrait_companion && optional_pair && !selected_companion && seed.portrait) {
      frame.companion = companion_for_(seed);
      selected_companion = frame.companion[0] != '\0';
    }
    const bool primary_last = (!optional_pair && !pairs_only) || (seed.orientation_known && !seed.portrait) ||
                              (remembered && frame.companion[0] == '\0');
    AssetId lookahead{};
    if (frame.companion[0] != '\0') {
      if constexpr (prefetch_portrait_companion)
        lookahead = frame.companion;
    } else if (primary_last) {
      lookahead = upcoming({});
    }
    auto primary_target = PreviewTarget::Slide;
    if (reframing_pair && !caption_only)
      primary_target = PreviewTarget::PairPane;
    else if (pairs_only)
      primary_target = PreviewTarget::RequiredPair;
    else if (optional_pair || frame.companion[0] != '\0')
      primary_target = PreviewTarget::OptionalPair;
    const auto status = this->load_preview_(frame.primary, primary, primary_target, 0, true, lookahead);
    if (this->cancelled_ || status == PreviewStatus::Retry)
      return retry_frame();
    if (status == PreviewStatus::Missing || (preserve_pair && reframing_pair && !primary.portrait()))
      return reject_candidate(frame.primary);

    // Optional pairing leaves the first preview on the fast path.
    const bool paired_primary = reframing_pair || (pairs_only && primary.portrait());
    const auto pair_deadline_us = paired_primary ? 0 : esp_timer_get_time() + (remembered ? 2 : 1) * pairing_time_us;
    if (paired_primary && frame.companion[0] == '\0') {
      if (!remembered) {
        const TrialStageTimer selection{TrialPreparationStage::Selection};
        frame.companion = this->companion_for_(seed, esp_timer_get_time() + pairing_time_us);
      }
      if (this->cancelled_)
        return retry_frame();
      if (frame.companion[0] == '\0') {
        this->source_status_ = PhotoSourceStatus::WaitingForPair;
        return reject_candidate(frame.primary);
      }
    }
    if (optional_pair && !selected_companion && primary.portrait()) {
      const TrialStageTimer selection{TrialPreparationStage::Selection};
      frame.companion = this->companion_for_(seed, pair_deadline_us);
    }

    DecodedPreview companion;
    if (frame.companion[0] != '\0' && primary.portrait()) {
      const auto right = this->load_preview_(frame.companion, companion, PreviewTarget::PairPane, pair_deadline_us,
                                             paired_primary, upcoming(frame.companion));
      if (right == PreviewStatus::Retry && (remembered || pairs_only))
        return retry_frame();
      if (preserve_pair && (right == PreviewStatus::Missing || !companion.portrait())) {
        if (!remembered && !settings_.has_prepared_slides() && unavailable_.contains(frame.companion, esp_timer_get_time()))
          this->pending_.push_back(seed);
        return reject_candidate(frame.companion);
      }
      if (right != PreviewStatus::Ready || !companion.portrait())
        frame.companion = {};
    } else {
      if constexpr (prefetch_portrait_companion) {
        if (frame.companion[0] != '\0')
          this->prefetch_.cancel();
      }
      frame.companion = {};
    }

    if (reframing_pair && frame.companion[0] == '\0') {
      // The primary was decoded for a pane. A missing optional companion
      // calls for a fresh crop of the original preview for the whole slide.
      const auto single = this->load_preview_(frame.primary, primary, PreviewTarget::Slide, 0, true);
      if (this->cancelled_ || single == PreviewStatus::Retry)
        return retry_frame();
      if (single == PreviewStatus::Missing)
        return reject_candidate(frame.primary);
    }
    this->prefetch_next_(upcoming(frame.companion));
    composition.emplace(TrialPreparationStage::Composition);
    if (!primary.render(panel, frame.companion[0] == '\0' ? nullptr : &companion, this->feed_().pair_display_mode(),
                        this->settings_.screen_rotation, this->checkpoint_))
      return retry_frame();
  } else {
    composition.emplace(TrialPreparationStage::Composition);
  }
  if (replacement && !caption_only)
    this->complete_caption_details_(frame);
  const auto &tone = this->settings_.tone;
  const auto local = captions.needs_time() || tone.night_enabled ? local_clock_time(std::time(nullptr)) : std::nullopt;
  if (tone.night_enabled && !this->tone_daylight_)
    this->tone_daylight_.emplace(find_clock_zone(this->settings_.clock.timezone));
  const float night_progress = this->tone_daylight_ ? this->tone_daylight_->night_warmth(local) : 0.0f;
  if (!tint_photo({panel, pixel_count}, tone.warmth(night_progress), this->checkpoint_) || !this->checkpoint_())
    return retry_frame();
  paint_photo_caption(panel, {caption_atlas, static_cast<size_t>(caption_atlas_end - caption_atlas)},
                      {date_atlas, static_cast<size_t>(date_atlas_end - date_atlas)}, frame.metadata, captions,
                      this->settings_.screen_rotation, this->settings_.clock.show, local);
  if (this->cancelled_)
    return retry_frame();
  pending_caption.time = captions.needs_time() && !local && frame.metadata.capture_day.has_value();
  pending_caption.details = this->needs_caption_details_(frame.metadata);
  if (!remembered && frame.companion[0] != '\0')
    std::erase_if(this->pending_, [&](const ImmichPhoto &photo) { return photo.id == frame.companion; });
  if (new_position) {
    this->history_.append(frame);
    if constexpr (direct_trial_replay)
      record_live_trial_replay_slide(frame.companion[0] != '\0');
  } else {
    *existing = frame;
  }
  return PrepareResult::Ready;
}

}  // namespace espframe::native
