#pragma once

#include "daylight.h"
#include "frame_status.h"
#include "immich_http.h"
#include "immich_selection.h"
#include "p4_jpeg_decoder.h"
#include "photo_caption.h"
#include "photo_history.h"
#include "preparation_checkpoint.h"
#include "preview_prefetch.h"
#include "recent_previews.h"
#include "settings.h"
#include "unavailable_previews.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace espframe::native {

class DecodedPreview;
class NativeNetwork;
enum class PreviewTarget;

// The slide preparation task owns the Immich HTTP client and photo order.
class ImmichSource {
 public:
  enum class PrepareResult { Ready, Retry, NextCandidate };
  enum class Preparation { FullPreview, AllowThumbnail, RefineThumbnail, CaptionOnly, RotateCached };
  struct PendingPresentation {
    PhotoCaption caption;
    bool caption_time{};
    bool caption_details{};
    bool thumbnail{};
    bool thumbnail_pair{};
    bool paired{};
  };

  ImmichSource(const Settings &settings, const NativeNetwork &network, const std::atomic<bool> &cancelled,
               PreparationCheckpoint checkpoint)
      : cancelled_(cancelled), checkpoint_(checkpoint), settings_(settings), network_(network), http_(cancelled) {}
  PrepareResult prepare(SlideIndex slide, uint16_t *panel, bool replacement, PendingPresentation &presentation,
                        Preparation preparation, ScreenRotation rotation);
  PendingPresentation describe_cached_slide(SlideIndex slide, ScreenRotation rotation);
  bool claim_preview_promotion() { return preview_promotable_.exchange(false, std::memory_order_acq_rel); }
  void end_preview_promotion() { preview_promotable_.store(false, std::memory_order_release); }
  void apply_photo_settings(PhotoFeedSettings feed, std::optional<SlideIndex> first_slide);
  void expire_warm_tone_override(const ClockZone &zone) {
    Settings::expire_warm_tone_override(zone.option, this->feed_());
  }
  SlideIndex first_slide() const { return this->history_.first(); }
  SlideIndex next_slide() const { return this->history_.next(); }
  PhotoSourceStatus source_status() const { return this->source_status_; }
  DisplayMode display_mode() const { return this->feed_().display_mode; }
  ImmichResponse response() const { return this->response_; }
  void probe_filter_support();

 private:
  enum class PreviewStatus { Ready, Missing, Retry };
  enum class PreparedWait { Bounded, Immediate, Legacy };
  struct PreparedPreview {
    bool ready{};
    bool orientation_mismatch{};
    bool pending{};
  };

  bool online_();
  void reset_transfers_();
  bool allocate_download_();
  bool select_search_api_(PhotoSourceMode mode);
  int discover_search_api_(int64_t deadline_us, bool report_response);
  int fetch_(std::string_view path, std::string_view body, size_t limit, size_t &received, int64_t deadline_us = 0,
             bool report_response = true);
  std::vector<ImmichPhoto> search_(const PhotoSource &source, std::optional<std::chrono::sys_days> local_day = {},
                                  std::optional<int64_t> pairing_deadline_us = {});
  bool update_capture_dates_();
  bool update_memory_day_();
  std::vector<ImmichPhoto> memories_();
  bool next_asset_(ImmichPhoto &asset);
  bool take_pending_(ImmichPhoto &asset);
  std::optional<size_t> pending_index_(const AssetId &exclude = {}) const;
  void prefetch_next_(const AssetId &id);
  bool select_pair_or_landscape_(ImmichPhoto &seed, AssetId &companion);
  AssetId companion_for_(const ImmichPhoto &primary, std::optional<int64_t> search_deadline = {},
                         const PhotoSlide *exclude = nullptr);
  void discard_candidate_(const AssetId &id);
  bool needs_caption_details_(const PhotoMetadata &metadata) const;
  void complete_caption_details_(PhotoSlide &frame);
  void describe_slide_(PendingPresentation &presentation, const PhotoMetadata &metadata,
                       ScreenRotation rotation, const std::optional<std::tm> &local) const;
  PreviewStatus load_preview_(const AssetId &id, DecodedPreview &preview, PreviewTarget target, int64_t deadline_us,
                              bool report_response, const AssetId &lookahead = {}, bool cached_only = false);
  bool load_thumbnail_(const AssetId &id, DecodedPreview &preview, PreviewTarget target, PhotoOrientation orientation,
                       bool use_prefetch = false);
  PreparedPreview render_prepared_(const PhotoSlide &frame, uint16_t *panel, PhotoOrientation orientation);
  std::string prepared_url_(const PhotoSlide &frame, PhotoOrientation orientation, bool frame_fill, DisplayMode framing) const;
  void warm_next_prepared_(const PhotoSlide &current);

  const std::atomic<bool> &cancelled_;
  std::atomic<bool> preview_promotable_{};
  const PreparationCheckpoint checkpoint_;
  const Settings &settings_;
  std::optional<PhotoConnectionSettings> connection_settings_;
  const PhotoConnectionSettings &connection_settings() const {
    return connection_settings_ ? *connection_settings_ : static_cast<const PhotoConnectionSettings &>(settings_);
  }
  ScreenRotation rotation_{};
  std::optional<PhotoFeedSettings> selection_;
  const PhotoFeedSettings &feed_() const { return selection_ ? *selection_ : settings_; }
  bool uses_prepared_slides_() const {
    return !portrait_screen(rotation_) && connection_settings().has_prepared_slides();
  }
  const NativeNetwork &network_;
  ImmichHttp http_;
  ImmichHttp prepared_http_{cancelled_, "Authorization"};
  PreviewPrefetch prefetch_{cancelled_};
  int64_t prepared_retry_us_{};
  int64_t prepared_frame_fill_retry_us_{};
  int64_t prepared_pair_retry_us_{};
  PreparedWait prepared_wait_{PreparedWait::Bounded};
  P4JpegDecoder jpeg_;
  uint32_t http_connection_{};
  uint32_t prefetch_connection_{1};
  ImmichSearchApi search_api_{ImmichSearchApi::Unknown};
  int64_t next_filter_probe_us_{};
  struct AlbumPage {
    uint32_t page{1};
    size_t count{photo_search_batch};
  };
  std::map<std::string, AlbumPage> album_pages_;
  size_t random_search_count_{photo_search_batch};
  ImmichFilterBranch filter_branch_;
  uint8_t next_filter_group_{};
  int next_filter_album_{};
  bool pending_is_paged_{};
  bool pending_has_next_page_{};
  uint8_t *encoded_{};
  PhotoHistory history_;
  RecentPreviews previews_;
  UnavailablePreviews unavailable_;
  Daylight tone_daylight_{nullptr};
  std::vector<ImmichPhoto> pending_;
  CaptureDateRange capture_date_range_;
  std::optional<std::chrono::sys_days> memory_day_;
  std::vector<ImmichPhoto> memory_pool_;
  int64_t memory_retry_at_{};
  unsigned next_memory_date_{};
  bool using_memory_fallback_{};
  bool memory_response_too_large_{};
  PhotoSourceStatus source_status_{PhotoSourceStatus::None};
  ImmichResponse response_{ImmichResponse::Pending};
};

}  // namespace espframe::native
