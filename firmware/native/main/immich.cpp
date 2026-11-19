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
                         int64_t deadline_us) {
  if (!online_()) {
    received = 0;
    return 0;
  }
  std::string url = this->settings_.immich_url;
  url += path;
  return this->http_.read(url, this->settings_.immich_key.c_str(), body, {this->encoded_, limit}, received, deadline_us);
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
  if (!online_())
    return {};
  if (!this->allocate_download_())
    return {};
  if (!pairing && !this->select_search_api_(source.mode))
    return {};

  const bool legacy_album = source.mode == PhotoSourceMode::Album && this->search_api_ == ImmichSearchApi::Legacy;
  uint32_t page = !pairing && legacy_album ? this->album_page_ : 1;
  size_t count = !pairing && legacy_album ? this->album_page_size_ : photo_search_batch;
  const bool with_exif = this->settings_.metadata.show_location || this->settings_.requires_portrait_pair();
  const auto dates = this->settings_.photo_source.mode == PhotoSourceMode::Memories
                         ? CaptureDateRange{} : this->capture_date_range_;
  auto request = make_photo_search(count, source, this->search_api_, local_day, page, with_exif, dates,
                                   this->settings_.portrait_pairing_days);
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
    request = make_photo_search(count, source, this->search_api_, local_day, page, with_exif, dates);
    status = this->fetch_(request.path, request.body, search_capacity, length);
  }
  if (!pairing && with_exif && (status == 400 || status == 413)) {
    request = make_photo_search(count, source, this->search_api_, local_day, page, false, dates);
    status = this->fetch_(request.path, request.body, search_capacity, length);
  }
  if (status != 200) {
    if constexpr (SWIPE_TRIAL_IMMICH)
      record_live_trial_search(pairing, status, 0);
    if (!pairing)
      ESP_LOGW(tag, "Photo search failed (%d)", status);
    return {};
  }
  auto response = parse_photo_search({reinterpret_cast<const char *>(this->encoded_), length}, count, request.paged,
                                     source.mode == PhotoSourceMode::Favorites, request.dates);
  if constexpr (SWIPE_TRIAL_IMMICH)
    record_live_trial_search(pairing, status, response.photos.size());
  if (!pairing && request.paged && response.valid)
    this->album_page_ = response.next_page > page ? response.next_page : 1;
  return std::move(response.photos);
}

bool ImmichSource::update_memory_day_() {
  const auto today = local_today();
  if (!today) {
    this->memories_status_ = MemoriesStatus::WaitingForTime;
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
    this->memories_status_ = MemoriesStatus::None;
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
    this->memories_status_ = MemoriesStatus::WaitingForTime;
    return false;
  }
  this->memories_status_ = MemoriesStatus::None;
  if (this->capture_date_range_ != *range) {
    this->capture_date_range_ = *range;
    this->pending_.clear();
    this->album_page_ = 1;
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
                                                   photo_search_batch) : std::nullopt;
      // A single oversized date should not prevent the rest of the window
      // from producing memories. Other request failures use the retry delay.
      if (!results && status != 413) {
        ESP_LOGW(tag, "On This Day request failed (%d). Check the Immich connection and memory.read permission.", status);
        this->memory_retry_at_ = esp_timer_get_time() + memory_retry_us;
        this->memories_status_ = MemoriesStatus::Unavailable;
        break;
      }
      this->memory_response_too_large_ |= status == 413;
      ++this->next_memory_date_;
      if (!results || results->empty())
        continue;
      this->memories_status_ = MemoriesStatus::None;
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
    this->memories_status_ = this->memory_response_too_large_ ? MemoriesStatus::Unavailable : MemoriesStatus::Empty;
    this->memory_response_too_large_ = false;
  }
  if (!this->memory_pool_.empty())
    return this->memory_pool_;
  this->using_memory_fallback_ = source.memory_fallback && esp_timer_get_time() < this->memory_retry_at_;
  auto fallback = this->using_memory_fallback_ ? this->search_(PhotoSource{}) : std::vector<ImmichPhoto>{};
  if (!fallback.empty())
    this->memories_status_ = MemoriesStatus::None;
  return fallback;
}

bool ImmichSource::next_asset_(ImmichPhoto &asset) {
  const auto &source = this->settings_.photo_source;
  const bool memory_mode = source.mode == PhotoSourceMode::Memories;
  if (!(memory_mode ? this->update_memory_day_() : this->update_capture_dates_()))
    return false;
  if (this->pending_.empty()) {
    this->pending_ = memory_mode ? this->memories_() : this->search_(source);
    if (this->settings_.requires_portrait_pair())
      std::erase_if(this->pending_, [](const ImmichPhoto &photo) { return !can_start_portrait_pair(photo); });
    if (this->pending_.empty())
      return false;
    if (source.mode == PhotoSourceMode::Album && this->search_api_ == ImmichSearchApi::Legacy) {
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
  if (this->settings_.requires_portrait_pair() &&
      (this->settings_.photo_source.mode != PhotoSourceMode::Album || this->search_api_ != ImmichSearchApi::Legacy)) {
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
    for (const auto &candidate : photos) {
      if (!is_portrait_companion(primary, candidate, this->settings_.portrait_pairing_days))
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
                                                        bool for_pair, int64_t deadline_us) {
  if (this->cancelled_)
    return PreviewStatus::Retry;
  const bool optional = deadline_us != 0;
  const auto decode = [&](const uint8_t *data, size_t length) {
    int64_t started_us = 0;
    if constexpr (SWIPE_TRIAL_IMMICH)
      started_us = esp_timer_get_time();
    const auto result = preview.decode(data, length, for_pair, &this->jpeg_, this->settings_.display_mode,
                                       this->checkpoint_);
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
      ESP_LOGI(tag, "Preview prepared from memory, bytes=%zu decode_us=%" PRId64 " hardware_jpeg=%d",
               cached.size(), esp_timer_get_time() - start, preview.hardware_jpeg());
      return PreviewStatus::Ready;
    }
    if (result == DecodedPreview::Result::NoMemory || result == DecodedPreview::Result::Interrupted)
      return PreviewStatus::Retry;
    this->previews_.discard(id);
  }
  if (!online_())
    return PreviewStatus::Retry;
  if (!this->allocate_download_())
    return PreviewStatus::Retry;

  const auto request_us = esp_timer_get_time();
  size_t length = 0;
  const auto path = std::string("/assets/") + id.data() + "/thumbnail?size=preview";
  const int status = this->fetch_(path, {}, photo_capacity, length, deadline_us);
  const auto decode_us = esp_timer_get_time();
  if constexpr (SWIPE_TRIAL_IMMICH) {
    record_live_trial_preview(status);
    record_live_trial_preparation_time(TrialPreparationStage::Transfer,
                                       static_cast<uint32_t>(decode_us - request_us), length);
  }
  if (status == 200 && !this->cancelled_) {
    const auto result = decode(this->encoded_, length);
    if (result == DecodedPreview::Result::Ready) {
      if (!this->checkpoint_())
        return PreviewStatus::Retry;
      this->previews_.remember(id, {this->encoded_, length});
      ESP_LOGI(tag, "Preview prepared, bytes=%zu fetch_us=%" PRId64 " decode_us=%" PRId64 " hardware_jpeg=%d",
               length, decode_us - request_us, esp_timer_get_time() - decode_us, preview.hardware_jpeg());
      return PreviewStatus::Ready;
    }
    if (result == DecodedPreview::Result::NoMemory || result == DecodedPreview::Result::Interrupted)
      return PreviewStatus::Retry;
  }
  if (this->cancelled_)
    return PreviewStatus::Retry;
  if (!optional)
    ESP_LOGW(tag, "Photo preview was unavailable or could not be decoded (%d)", status);
  return status == 404 || status == 410 || status == 413 || status == 200 ? PreviewStatus::Missing
                                                                      : PreviewStatus::Retry;
}

ImmichSource::PrepareResult ImmichSource::prepare(SlideIndex slide, uint16_t *panel) {
  const TrialStageTimer attempt{TrialPreparationStage::Attempt};
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
    if (pairs_only && !this->select_pair_(seed, frame.companion))
      return PrepareResult::Retry;
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
  DecodedPreview primary;
  const auto status = this->load_preview_(frame.primary, primary, pairs_only);
  if (this->cancelled_ || status == PreviewStatus::Retry)
    return retry_frame();
  if (status == PreviewStatus::Missing || (pairs_only && !primary.portrait()))
    return reject_candidate(frame.primary);

  // Optional pairing leaves the first preview on the fast path.
  const auto pair_deadline_us = pairs_only ? 0 : esp_timer_get_time() + (remembered ? 2 : 1) * pairing_time_us;
  if (!remembered && !pairs_only && slide > 1 && this->settings_.portrait_pairing && primary.portrait()) {
    const TrialStageTimer selection{TrialPreparationStage::Selection};
    frame.companion = this->companion_for_(seed, pair_deadline_us);
  }

  DecodedPreview companion;
  if (frame.companion[0] != '\0' && primary.portrait()) {
    const auto right = this->load_preview_(frame.companion, companion, true, pair_deadline_us);
    if (right == PreviewStatus::Retry && (remembered || pairs_only))
      return retry_frame();
    if (pairs_only && (right == PreviewStatus::Missing || !companion.portrait()))
      return reject_candidate(frame.companion);
    if (right != PreviewStatus::Ready || !companion.portrait())
      frame.companion = {};
  } else {
    frame.companion = {};
  }

  const TrialStageTimer composition{TrialPreparationStage::Composition};
  if (!primary.render(panel, frame.companion[0] == '\0' ? nullptr : &companion, this->settings_.display_mode,
                      this->settings_.screen_rotation, this->checkpoint_))
    return retry_frame();
  const auto &captions = this->settings_.metadata;
  const auto &tone = this->settings_.tone;
  const auto local = captions.needs_time() || tone.night_enabled ? local_clock_time(std::time(nullptr)) : std::nullopt;
  if (tone.night_enabled && !this->tone_daylight_)
    this->tone_daylight_.emplace(find_clock_zone(this->settings_.clock.timezone));
  const float night_progress = this->tone_daylight_ ? this->tone_daylight_->night_warmth(local) : 0.0f;
  if (!tint_photo({panel, pixel_count}, tone.warmth(night_progress), this->checkpoint_) || !this->checkpoint_())
    return retry_frame();
  paint_photo_caption(panel, {caption_atlas, static_cast<size_t>(caption_atlas_end - caption_atlas)},
                      frame.metadata, captions, this->settings_.screen_rotation, local);
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
