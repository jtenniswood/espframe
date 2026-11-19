#pragma once

#include "daylight.h"
#include "immich_http.h"
#include "immich_selection.h"
#include "p4_jpeg_decoder.h"
#include "photo_history.h"
#include "preparation_checkpoint.h"
#include "recent_previews.h"
#include "settings.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace espframe::native {

class DecodedPreview;
class NativeNetwork;

// The slide preparation task owns the Immich HTTP client and photo order.
class ImmichSource {
 public:
  enum class PrepareResult { Ready, Retry, NextCandidate };

  ImmichSource(const Settings &settings, const NativeNetwork &network, const std::atomic<bool> &cancelled,
               const std::atomic<bool> &pixel_work_deferred)
      : cancelled_(cancelled), pixel_work_deferred_(pixel_work_deferred), settings_(settings), network_(network),
        http_(cancelled) {}
  PrepareResult prepare(SlideIndex slide, uint16_t *panel);
  SlideIndex first_slide() const { return this->history_.first(); }
  SlideIndex next_slide() const { return this->history_.next(); }
  MemoriesStatus memories_status() const { return this->memories_status_; }

 private:
  enum class PreviewStatus { Ready, Missing, Retry };

  bool online_();
  bool allocate_download_();
  bool select_search_api_(PhotoSourceMode mode);
  int fetch_(std::string_view path, std::string_view body, size_t limit, size_t &received, int64_t deadline_us = 0);
  std::vector<ImmichPhoto> search_(const PhotoSource &source, std::optional<std::chrono::sys_days> local_day = {},
                                  int64_t deadline_us = 0);
  bool update_capture_dates_();
  bool update_memory_day_();
  std::vector<ImmichPhoto> memories_();
  bool next_asset_(ImmichPhoto &asset);
  bool take_pending_(ImmichPhoto &asset);
  bool select_pair_(ImmichPhoto &seed, AssetId &companion);
  AssetId companion_for_(const ImmichPhoto &primary, int64_t deadline_us);
  void discard_candidate_(const AssetId &id);
  bool wait_for_pixel_work_() const;
  PreviewStatus load_preview_(const AssetId &id, DecodedPreview &preview, bool for_pair, int64_t deadline_us = 0);
  bool render_prepared_(const PhotoSlide &frame, uint16_t *panel);

  const std::atomic<bool> &cancelled_;
  const std::atomic<bool> &pixel_work_deferred_;
  const PreparationCheckpoint checkpoint_{this, [](void *source) {
    return static_cast<const ImmichSource *>(source)->wait_for_pixel_work_();
  }};
  const Settings &settings_;
  const NativeNetwork &network_;
  ImmichHttp http_;
  ImmichHttp prepared_http_{cancelled_, "Authorization"};
  int64_t prepared_retry_us_{};
  P4JpegDecoder jpeg_;
  uint32_t http_connection_{};
  ImmichSearchApi search_api_{ImmichSearchApi::Unknown};
  uint32_t album_page_{1};
  size_t album_page_size_{photo_search_batch};
  uint8_t *encoded_{};
  PhotoHistory history_;
  RecentPreviews previews_;
  std::optional<Daylight> tone_daylight_;
  std::vector<ImmichPhoto> pending_;
  CaptureDateRange capture_date_range_;
  std::optional<std::chrono::sys_days> memory_day_;
  std::vector<ImmichPhoto> memory_pool_;
  int64_t memory_retry_at_{};
  unsigned next_memory_date_{};
  bool using_memory_fallback_{};
  bool memory_response_too_large_{};
  MemoriesStatus memories_status_{MemoriesStatus::None};
};

}  // namespace espframe::native
