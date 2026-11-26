#include "immich.h"

#include "immich_memories.h"
#include "live_trial.h"
#include "native_network.h"
#include "photo_caption.h"
#include "preview_decoder.h"
#include "trial_replay.h"
#include "trial_pair_fill.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

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

bool wants_search_exif(const PhotoFeedSettings &feed) {
  return feed.metadata.show_location || feed.requires_portrait_pair();
}

}  // namespace

void ImmichSource::apply_photo_settings(PhotoFeedSettings feed, std::optional<SlideIndex> first_slide) {
  this->prefetch_.cancel();
  if (feed.pending_connection && *feed.pending_connection != connection_settings()) {
    connection_settings_ = std::move(feed.pending_connection);
    reset_transfers_();
    previews_.clear();
    unavailable_.clear();
    search_api_ = ImmichSearchApi::Unknown;
    next_filter_probe_us_ = 0;
    active_immich_search_api.store(search_api_, std::memory_order_relaxed);
    frame_status.begin_immich_connection();
  }
  feed.pending_connection.reset();
  if (this->feed_().photo_orientation != feed.photo_orientation) {
    this->prepared_retry_us_ = 0;
    this->prepared_frame_fill_retry_us_ = 0;
    this->prepared_pair_retry_us_ = 0;
  }
  if (first_slide || wants_search_exif(this->feed_()) != wants_search_exif(feed))
    this->random_search_count_ = photo_search_batch;
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

void ImmichSource::reset_transfers_() {
  http_.reset();
  prepared_http_.reset();
  prefetch_.cancel();
  if (++prefetch_connection_ == 0)
    ++prefetch_connection_;
  prepared_retry_us_ = 0;
  prepared_frame_fill_retry_us_ = 0;
  prepared_pair_retry_us_ = 0;
  prepared_wait_ = PreparedWait::Bounded;
}

bool ImmichSource::online_() {
  const auto connection = network_.photo_connection();
  if (connection != http_connection_) {
    reset_transfers_();
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
  std::string url = this->connection_settings().immich_url;
  url += path;
  const int status = this->http_.read(url, this->connection_settings().immich_key.c_str(), body, {this->encoded_, limit},
                                      received, deadline_us);
  if constexpr (!SWIPE_ESPHOME_TRIAL) {
    if (report_response && !cancelled_)
      this->response_ = immich_response(status);
  }
  return status;
}

int ImmichSource::discover_search_api_(int64_t deadline_us, bool report_response) {
  size_t length;
  const int status = this->fetch_("/server/version", {}, 1024, length, deadline_us, report_response);
  if (status == 200) {
    this->search_api_ = parse_search_api({reinterpret_cast<const char *>(this->encoded_), length});
    if constexpr (!SWIPE_ESPHOME_TRIAL) {
      active_immich_search_api.store(this->search_api_, std::memory_order_relaxed);
    }
  }
  return status;
}

void ImmichSource::probe_filter_support() {
  const auto now = esp_timer_get_time();
  if (this->search_api_ != ImmichSearchApi::Unknown || now < this->next_filter_probe_us_ || !this->encoded_)
    return;
  this->next_filter_probe_us_ = now + 60'000'000;
  this->discover_search_api_(now + 350'000, false);
}

bool ImmichSource::select_search_api_(PhotoSourceMode mode) {
  const bool library = mode == PhotoSourceMode::Library;
  if ((library && !this->capture_date_range_.active()) || this->search_api_ != ImmichSearchApi::Unknown)
    return true;
  const int status = this->discover_search_api_(0, true);
  if (this->search_api_ == ImmichSearchApi::Unknown) {
    ESP_LOGW(tag, "Could not determine Immich search capabilities for the selected photo source (%d)", status);
    return library;
  }
  return true;
}

std::vector<ImmichPhoto> ImmichSource::search_(const PhotoSource &source,
                                              std::optional<std::chrono::sys_days> local_day,
                                              std::optional<int64_t> pairing_deadline_us) {
  const bool pairing = pairing_deadline_us.has_value();
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
  size_t count = pairing ? photo_search_batch : this->random_search_count_;
  if (album_page)
    count = album_page->count;
  const bool with_exif = pairing || wants_search_exif(this->feed_());
  const auto dates = this->feed_().photo_source.mode == PhotoSourceMode::Memories
                         ? CaptureDateRange{} : this->capture_date_range_;
  const auto make_request = [&](bool exif) {
    const auto pairing_days = this->feed_().portrait_pairing_days();
    return custom ? make_filtered_photo_search(count, filters, this->filter_branch_, this->search_api_, local_day,
                                               page, exif, dates, pairing_days)
                  : make_photo_search(count, source, this->search_api_, local_day, page, exif, dates, pairing_days);
  };
  auto request = make_request(with_exif);
  if (request.path.empty())
    return {};
  size_t length;
  int status = this->fetch_(request.path, request.body, search_capacity, length, pairing_deadline_us.value_or(0),
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
  if (!pairing && !paged && response.valid) {
    // Keep a size that worked for this random feed. A large EXIF response
    // can exceed the search buffer every time, even with different photos.
    this->random_search_count_ = count;
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
        return !can_play_with_required_pairs(photo, this->feed_().photo_orientation,
                                             this->feed_().portrait_pairing_days());
      });
      if (removed != 0 && this->pending_.empty() && !this->pending_has_next_page_)
        this->source_status_ = PhotoSourceStatus::WaitingForPair;
    }
    // A prepared slide can still work when the direct preview is unavailable.
    if (!this->uses_prepared_slides_()) {
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
    if (const auto match = find_pair_or_landscape_seed(this->pending_, this->feed_().portrait_pairing_days(), exclude,
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
  const auto &connection = connection_settings();
  this->prefetch_.start(id, prefetch_connection_, connection.immich_url, connection.immich_key);
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

AssetId ImmichSource::companion_for_(const ImmichPhoto &primary, std::optional<int64_t> search_deadline,
                                    const PhotoSlide *exclude) {
  const auto pairing_days = this->feed_().portrait_pairing_days();
  if (!primary.metadata.capture_day && pairing_days != portrait_pairing_any_time)
    return {};
  auto choose = [&](const std::vector<ImmichPhoto> &photos) {
    AssetId best{};
    auto closest = std::chrono::days::max();
    bool best_seen = true;
    const auto now = esp_timer_get_time();
    for (const auto &candidate : photos) {
      if (exclude && (candidate.id == exclude->primary || candidate.id == exclude->companion))
        continue;
      if (!is_portrait_companion(primary, candidate, pairing_days))
        continue;
      if (!this->uses_prepared_slides_() && this->unavailable_.contains(candidate.id, now))
        continue;
      const bool seen = this->history_.recently_seen(candidate.id, photo_search_batch);
      const auto distance = candidate.metadata.capture_day && primary.metadata.capture_day
                                ? std::chrono::abs(*candidate.metadata.capture_day - *primary.metadata.capture_day)
                                : std::chrono::days::max();
      if (best[0] == '\0' || (!seen && best_seen) || (seen == best_seen && distance < closest)) {
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

ImmichSource::PreviewStatus ImmichSource::load_preview_(const AssetId &id, DecodedPreview &preview,
                                                        PreviewTarget target, int64_t deadline_us, bool report_response,
                                                        const AssetId &lookahead, bool cached_only) {
  if (this->cancelled_)
    return PreviewStatus::Retry;
  const bool optional = deadline_us != 0;
  const bool for_pair = target == PreviewTarget::PairPane;
  const auto orientation = for_pair ? PhotoOrientation::Portrait : this->feed_().photo_orientation;
  const PreviewLayout layout{target, this->feed_().display_mode, this->feed_().pair_display_mode(),
                             this->rotation_};
  const auto decode = [&](const uint8_t *data, size_t length) {
    if (!cached_only && (is_jpeg_preview(data, length) || is_webp_preview(data, length)))
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
  if (cached_only || !online_())
    return PreviewStatus::Retry;
  if (this->unavailable_.contains(id, esp_timer_get_time()))
    return PreviewStatus::Missing;
  if (!this->allocate_download_())
    return PreviewStatus::Retry;

  const auto request_us = esp_timer_get_time();
  const auto staged = !prefetch_portrait_companion && optional
                          ? std::nullopt
                          : this->prefetch_.take(id, prefetch_connection_, {this->encoded_, photo_capacity}, deadline_us);
  size_t length = staged.value_or(0);
  int status = length ? 200 : 0;
  if (staged) {
    if constexpr (SWIPE_TRIAL_IMMICH) {
      if (length)
        record_live_trial_preparation_time(TrialPreparationStage::PrefetchWait,
                                           static_cast<uint32_t>(esp_timer_get_time() - request_us));
    }
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

bool ImmichSource::load_thumbnail_(const AssetId &id, DecodedPreview &preview, PreviewTarget target,
                                  PhotoOrientation orientation, bool use_prefetch) {
  constexpr auto thumbnail_capacity = PreviewPrefetch::thumbnail_capacity;
  // Thumbnails are transient pixels. A separate request size must not seed
  // the encoded preview cache used to upgrade or revisit this slide.
  if (cancelled_ || !online_() || !allocate_download_())
    return false;
  const auto started = esp_timer_get_time();
  const auto deadline = started + PreviewPrefetch::thumbnail_budget_us;
  const auto staged = use_prefetch ? prefetch_.take(id, prefetch_connection_, {encoded_, thumbnail_capacity}, deadline,
                                                   PreviewPrefetch::Size::Thumbnail)
                                   : std::nullopt;
  size_t length = 0;
  if (staged) {
    length = *staged;
    if (!length)
      return false;
    if constexpr (SWIPE_TRIAL_IMMICH)
      record_live_trial_preparation_time(TrialPreparationStage::ThumbnailPrefetchWait,
                                         static_cast<uint32_t>(esp_timer_get_time() - started), length);
  } else if (!cancelled_) {
    const auto path = std::string("/assets/") + id.data() + "/thumbnail?size=thumbnail";
    const auto status = fetch_(path, {}, thumbnail_capacity, length, deadline, false);
    if constexpr (SWIPE_TRIAL_IMMICH)
      record_live_trial_preparation_time(TrialPreparationStage::ThumbnailTransfer,
                                         static_cast<uint32_t>(esp_timer_get_time() - started), length);
    if (status != 200)
      return false;
  }
  if (cancelled_)
    return false;
  const PreviewLayout layout{target, feed_().display_mode, feed_().pair_display_mode(), rotation_};
  return preview.decode(encoded_, length, layout, nullptr, checkpoint_, orientation) ==
         DecodedPreview::Result::Ready;
}

std::string ImmichSource::prepared_url_(const PhotoSlide &frame, PhotoOrientation orientation, bool frame_fill,
                                        DisplayMode framing) const {
  std::string url(connection_settings().prepared_slide_url);
  url += "/v1/slides/";
  url += frame.primary.data();
  url += frame_fill ? "?mode=fill-frame" : framing == DisplayMode::Fill ? "?mode=fill" : "?mode=fit";
  if (frame.companion[0] != '\0') {
    url += "&companion=";
    url += frame.companion.data();
    url += "&gap=0";
  }
  url += "&rotation=";
  url += screen_rotation_name(rotation_);
  if (orientation != PhotoOrientation::Any)
    url += orientation == PhotoOrientation::Portrait ? "&orientation=portrait" : "&orientation=landscape";
  return url;
}

void ImmichSource::warm_next_prepared_(const PhotoSlide &current) {
  if (!online_() || prepared_wait_ == PreparedWait::Legacy || esp_timer_get_time() < prepared_retry_us_ ||
      (connection_settings().prepared_slides_need_time() && !clock_time_is_valid(std::time(nullptr))))
    return;
  const auto index = pending_index_(current.companion);
  if (!index)
    return;
  const auto &seed = pending_[*index];
  const auto &feed = feed_();
  if (!seed.orientation_known || !seed.may_match_orientation(feed.photo_orientation))
    return;
  PhotoSlide next{seed.id};
  if (seed.portrait && feed.pairs_portraits()) {
    next.companion = companion_for_(seed, {}, &current);
    if (next.companion[0] == '\0')
      return;  // A companion that needs more metadata will be selected by the slide worker.
  }
  const bool paired = next.companion[0] != '\0';
  if (paired && esp_timer_get_time() < prepared_pair_retry_us_)
    return;
  if (feed.requires_portrait_pair() && !paired && feed.photo_orientation != PhotoOrientation::Any)
    return;
  const auto orientation = feed.requires_portrait_pair() && !paired ? PhotoOrientation::Landscape
                                                                   : feed.photo_orientation;
  const auto framing = paired ? feed.pair_display_mode() : feed.display_mode;
  const bool frame_fill = !paired && framing == DisplayMode::Fill && orientation != PhotoOrientation::Landscape;
  const RecentPreviews::PreparedKey key{next.primary, next.companion, framing, orientation, rotation_};
  if (previews_.find(key).empty())
    prefetch_.warm_prepared(prepared_url_(next, orientation, frame_fill, framing), prefetch_connection_,
                            connection_settings().prepared_slide_token);
}

ImmichSource::PreparedPreview ImmichSource::render_prepared_(const PhotoSlide &frame, uint16_t *panel,
                                                            PhotoOrientation orientation) {
  const bool paired = frame.companion[0] != '\0';
  const auto framing = paired ? this->feed_().pair_display_mode() : this->feed_().display_mode;
  const bool frame_fill = !paired && framing == DisplayMode::Fill && orientation != PhotoOrientation::Landscape;
  const RecentPreviews::PreparedKey key{frame.primary, frame.companion, framing, orientation, rotation_};
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
      (paired && now < prepared_pair_retry_us_) ||
      (connection_settings().prepared_slides_need_time() && !clock_time_is_valid(std::time(nullptr))) ||
      !allocate_download_())
    return {};
  const auto url = prepared_url_(frame, orientation, frame_fill, framing);
  const auto authorization = "Bearer " + connection_settings().prepared_slide_token;
  const auto start = esp_timer_get_time();
  size_t length = 0;
  const bool background = prepared_wait_ != PreparedWait::Legacy && !unavailable_.contains(frame.primary, now) &&
                          (!paired || !unavailable_.contains(frame.companion, now));
  const auto request = [&](const std::string &target) {
    return prepared_http_.read(target, authorization.c_str(), {}, {encoded_, photo_capacity}, length, start + 1'500'000);
  };
  const auto wait = background ? prepared_wait_ : PreparedWait::Legacy;
  int status = request(wait == PreparedWait::Bounded ? url + "&wait=180"
                       : wait == PreparedWait::Immediate ? url + "&wait=0" : url);
  if (!paired && status == 400 && wait == PreparedWait::Bounded && !cancelled_) {
    // Immediate background requests work with the previous service protocol.
    prepared_wait_ = PreparedWait::Immediate;
    status = request(url + "&wait=0");
  }
  if (!paired && status == 400 && background && !cancelled_) {
    // The original service supports only a blocking request.
    prepared_wait_ = PreparedWait::Legacy;
    status = request(url);
  }
  const auto decode_start = esp_timer_get_time();
  if constexpr (SWIPE_TRIAL_IMMICH)
    record_live_trial_preparation_time(TrialPreparationStage::PreparedTransfer,
                                       static_cast<uint32_t>(decode_start - start), length);
  if (background && status == 202)
    return {.pending = true};
  if (background && prepared_wait_ != PreparedWait::Legacy && status == 429)
    return {};
  if (!cancelled_ && status == 400 && paired) {
    // The old service rejects gap=0. Compose pairs on the frame and keep
    // using the service for singles.
    prepared_pair_retry_us_ = decode_start + 30'000'000;
    return {};
  }
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

void ImmichSource::describe_slide_(PendingPresentation &presentation, const PhotoMetadata &metadata,
                                    ScreenRotation rotation, const std::optional<std::tm> &local) const {
  const auto &captions = feed_().metadata;
  const bool show_clock = SWIPE_ESPHOME_TRIAL ? settings_.clock.show : active_clock_visible.load(std::memory_order_relaxed);
  presentation.caption = prepare_photo_caption({caption_atlas, static_cast<size_t>(caption_atlas_end - caption_atlas)},
                                               {date_atlas, static_cast<size_t>(date_atlas_end - date_atlas)},
                                               metadata, captions, show_clock, local, rotation);
  presentation.caption_time = captions.needs_time() && !local && metadata.capture_day.has_value();
  presentation.caption_details = needs_caption_details_(metadata);
}

ImmichSource::PendingPresentation ImmichSource::describe_cached_slide(SlideIndex slide, ScreenRotation rotation) {
  PendingPresentation presentation;
  if (const auto *frame = history_.find(slide)) {
    const auto local = feed_().metadata.needs_time() ? local_clock_time(std::time(nullptr)) : std::nullopt;
    describe_slide_(presentation, frame->metadata, rotation, local);
  }
  return presentation;
}

ImmichSource::PrepareResult ImmichSource::prepare(SlideIndex slide, uint16_t *panel, bool replacement,
                                                 PendingPresentation &presentation, Preparation preparation,
                                                 ScreenRotation rotation) {
  const TrialStageTimer attempt{TrialPreparationStage::Attempt};
  this->rotation_ = rotation;
  const bool rotating = preparation == Preparation::RotateCached;
  if constexpr (!SWIPE_ESPHOME_TRIAL) {
    if (!rotating)
      this->response_ = ImmichResponse::Pending;
  }
  if (this->cancelled_)
    return PrepareResult::Retry;
  const bool new_position = slide == this->history_.next();
  auto *existing = this->history_.find(slide);
  const bool remembered = existing != nullptr && existing->primary[0] != '\0';
  if (replacement && !remembered)
    return PrepareResult::NextCandidate;
  if (!new_position && existing == nullptr)
    return PrepareResult::Retry;
  const bool allows_pairing = !portrait_screen(rotation) && this->feed_().pairs_portraits();
  const bool reframing_pair = replacement && allows_pairing && existing->companion[0] != '\0';
  const bool pairs_only = allows_pairing && this->feed_().requires_portrait_pair();
  const bool caption_only = preparation == Preparation::CaptionOnly;
  const bool refining = preparation == Preparation::RefineThumbnail;
  const bool preserve_pair = pairs_only || (reframing_pair && (caption_only || refining));
  ImmichPhoto seed;
  PhotoSlide frame;
  if (remembered) {
    frame = *existing;
    if (!allows_pairing)
      frame.companion = {};
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
  bool prepared_pending = false;
  const bool optional_pair = !remembered && !pairs_only && slide > 1 && allows_pairing;
  bool selected_companion = false;
  const bool pair_needs_selection = optional_pair && seed.portrait;
  // Ask the service to verify a single landscape before using an opaque
  // prepared slide in a feed that must never show a single portrait.
  const bool verify_landscape = pairs_only && frame.companion[0] == '\0';
  const bool may_prepare_landscape = feed_().photo_orientation == PhotoOrientation::Any &&
                                    (remembered || (seed.orientation_known && !seed.portrait));
  if (!rotating && uses_prepared_slides_() && (!optional_pair || seed.orientation_known) &&
      (!verify_landscape || may_prepare_landscape) &&
      (!pair_needs_selection || esp_timer_get_time() >= prepared_retry_us_)) {
    if (pair_needs_selection) {
      const TrialStageTimer selection{TrialPreparationStage::Selection};
      frame.companion = companion_for_(seed, esp_timer_get_time() + pairing_time_us);
      selected_companion = true;
    }
    const auto prepared_orientation = verify_landscape ? PhotoOrientation::Landscape : feed_().photo_orientation;
    if (new_position)
      this->warm_next_prepared_(frame);
    const auto preview = this->render_prepared_(frame, panel, prepared_orientation);
    if (preview.orientation_mismatch && !verify_landscape)
      return reject_candidate(frame.primary);
    prepared = preview.ready;
    prepared_pending = preview.pending;
  }
  if (this->cancelled_)
    return retry_frame();
  // Start a known companion during primary decode. A new Immich search
  // still waits for the decoded portrait so it cannot delay a landscape.
  if (!prepared && prefetch_portrait_companion && optional_pair && !selected_companion && seed.portrait) {
    frame.companion = companion_for_(seed);
    selected_companion = frame.companion[0] != '\0';
  }
  std::optional<TrialStageTimer> composition;
  bool thumbnail = false;
  const bool thumbnail_pair = seed.portrait && frame.companion[0] != '\0';
  // The all-pairs replay is a fixed comparison. The adaptive player can
  // stage single photos whose layout needs no companion lookup.
  const bool thumbnail_single = frame.companion[0] == '\0' && (!trial_replay_staged_detail || SWIPE_TRIAL_REPLAY_EDGE_DETAIL) &&
                                  seed.can_stage_unpaired(feed_().photo_orientation, allows_pairing);
  // A prepared image already supplies full detail. If the service couldn't
  // deliver it, keep the usual fast refill available from Immich.
  const bool can_stage = !prepared && new_position && !remembered && seed.orientation_known &&
                         (thumbnail_pair || thumbnail_single);
  const auto thumbnail_orientation = seed.portrait ? PhotoOrientation::Portrait : PhotoOrientation::Landscape;
  if (preparation == Preparation::AllowThumbnail && can_stage) {
    DecodedPreview left, right;
    const auto target = thumbnail_pair ? PreviewTarget::PairPane : PreviewTarget::Slide;
    // Both IDs are known. Share the same background slot as full previews
    // so the second thumbnail can load while the first transfers and decodes.
    const auto &connection = connection_settings();
    const bool companion_prefetched = prefetch_portrait_companion && thumbnail_pair && online_() &&
                                      prefetch_.start(frame.companion, prefetch_connection_, connection.immich_url,
                                                      connection.immich_key, PreviewPrefetch::Size::Thumbnail);
    thumbnail = load_thumbnail_(frame.primary, left, target, thumbnail_orientation) &&
                (!thumbnail_pair || load_thumbnail_(frame.companion, right, PreviewTarget::PairPane,
                                                     thumbnail_orientation, companion_prefetched));
    if (!thumbnail && companion_prefetched)
      prefetch_.cancel();
    if (thumbnail) {
      composition.emplace(TrialPreparationStage::Composition);
      if (!left.render(panel, thumbnail_pair ? &right : nullptr, feed_().pair_display_mode(), rotation_,
                       checkpoint_))
        return retry_frame();
    }
    if (cancelled_)
      return retry_frame();
  }
  if (!prepared && !thumbnail) {
    const auto upcoming = [&](const AssetId &exclude) {
      if (!SWIPE_TRIAL_REPLAY_DISABLE_LOOKAHEAD && !remembered && !uses_prepared_slides_()) {
        if (const auto index = this->pending_index_(exclude))
          return this->pending_[*index].id;
      }
      return AssetId{};
    };
    DecodedPreview primary;
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
    // An edge drag can switch an existing speculative request to thumbnails.
    // A failed thumbnail attempt falls back to a full preview just once.
    if (can_stage && preparation == Preparation::FullPreview)
      this->preview_promotable_.store(true, std::memory_order_release);
    const auto status = this->load_preview_(frame.primary, primary, primary_target, 0, !rotating, lookahead, rotating);
    if (this->cancelled_ || status == PreviewStatus::Retry)
      return retry_frame();
    if (status == PreviewStatus::Missing && prepared_pending && unavailable_.contains(frame.primary, esp_timer_get_time()))
      return retry_frame();
    if (status == PreviewStatus::Missing || (preserve_pair && reframing_pair && !primary.portrait()) ||
        (refining && !reframing_pair && !matches_photo_orientation(frame.thumbnail_orientation, primary.portrait())))
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
                                             paired_primary && !rotating, upcoming(frame.companion), rotating);
      if (right == PreviewStatus::Retry && (remembered || pairs_only))
        return retry_frame();
      if (right == PreviewStatus::Missing && preserve_pair && prepared_pending &&
          unavailable_.contains(frame.companion, esp_timer_get_time()))
        return retry_frame();
      if (preserve_pair && (right == PreviewStatus::Missing || !companion.portrait())) {
        if (!remembered && !uses_prepared_slides_() && unavailable_.contains(frame.companion, esp_timer_get_time()))
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
      const auto single = this->load_preview_(frame.primary, primary, PreviewTarget::Slide, 0, !rotating, {}, rotating);
      if (this->cancelled_ || single == PreviewStatus::Retry)
        return retry_frame();
      if (single == PreviewStatus::Missing)
        return reject_candidate(frame.primary);
    }
    if (!rotating)
      this->prefetch_next_(upcoming(frame.companion));
    this->end_preview_promotion();
    composition.emplace(TrialPreparationStage::Composition);
    if (!primary.render(panel, frame.companion[0] == '\0' ? nullptr : &companion, this->feed_().pair_display_mode(),
                        this->rotation_, this->checkpoint_))
      return retry_frame();
  } else if (!composition) {
    composition.emplace(TrialPreparationStage::Composition);
  }
  const bool paired = frame.companion[0] != '\0';
  const bool fill_edges_match = !trial_replay_pair_fill || !new_position || !paired ||
                                replay_pair_fills_panes(panel, this->rotation_);
  if (replacement && !caption_only && !refining && !rotating)
    this->complete_caption_details_(frame);
  const auto &tone = this->feed_().tone;
  const auto clock = captions.needs_time() || tone.night_enabled ? local_clock_reading(std::time(nullptr)) : LocalClockReading{};
  tone_daylight_.set_zone(clock.zone);
  const float night_progress = tone.night_enabled ? tone_daylight_.night_warmth(clock.time) : 0.0f;
  if (!tint_photo({panel, pixel_count}, tone.warmth(night_progress), this->checkpoint_) || !this->checkpoint_())
    return retry_frame();
  if (paired)
    paint_portrait_pair_divider(panel, rotation, this->feed_().pair_divider);
  describe_slide_(presentation, frame.metadata, rotation, clock.time);
  if (this->cancelled_)
    return retry_frame();
  presentation.thumbnail = thumbnail;
  presentation.paired = paired;
  frame.thumbnail_orientation = thumbnail ? thumbnail_orientation : PhotoOrientation::Any;
  presentation.thumbnail_pair = thumbnail && paired;
  if (rotating)
    return PrepareResult::Ready;
  if (!remembered && frame.companion[0] != '\0')
    std::erase_if(this->pending_, [&](const ImmichPhoto &photo) { return photo.id == frame.companion; });
  if (new_position) {
    this->history_.append(frame);
    if constexpr (synthetic_trial_replay)
      record_live_trial_replay_slide(paired, fill_edges_match);
  } else {
    *existing = frame;
  }
  return PrepareResult::Ready;
}

}  // namespace espframe::native
