#pragma once

#include "immich_http.h"
#include "immich_selection.h"
#include "p4_jpeg_decoder.h"
#include "photo_history.h"
#include "recent_previews.h"
#include "settings.h"

#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace espframe::native {

class DecodedPreview;

// Settings are optional. This source is owned by the slide preparation task
// after startup, including the HTTP client and per-session photo order.
class ImmichSource {
 public:
  explicit ImmichSource(const std::atomic<bool> &cancelled) : cancelled_(cancelled), http_(cancelled) {}
  bool read_settings();
  bool prepare(SlideIndex slide, uint16_t *panel);
  SlideIndex first_slide() const { return this->history_.first(); }
  uint32_t slide_seconds() const { return this->settings_.slide_seconds; }
  const ClockSettings &clock() const { return this->settings_.clock; }

 private:
  enum class PreviewStatus { Ready, Missing, Retry };

  static void wifi_event_(void *argument, esp_event_base_t base, int32_t event, void *);
  bool start_wifi_();
  bool online_();
  bool has_ip_() const;
  bool allocate_download_();
  bool select_search_api_();
  int fetch_(std::string_view path, std::string_view body, size_t limit, size_t &received, int64_t deadline_us = 0);
  std::vector<ImmichPhoto> search_(std::optional<std::chrono::sys_days> local_day = {}, int64_t deadline_us = 0);
  bool next_asset_(ImmichPhoto &asset);
  AssetId companion_for_(const ImmichPhoto &primary, int64_t deadline_us);
  PreviewStatus load_preview_(const AssetId &id, DecodedPreview &preview, bool companion, int64_t deadline_us = 0);

  const std::atomic<bool> &cancelled_;
  Settings settings_;
  ImmichHttp http_;
  P4JpegDecoder jpeg_;
  EventGroupHandle_t wifi_events_{};
  bool wifi_started_{};
  bool wifi_ready_{};
  bool sntp_started_{};
  ImmichSearchApi search_api_{ImmichSearchApi::Unknown};
  uint32_t album_page_{1};
  size_t album_page_size_{photo_search_batch};
  uint8_t *encoded_{};
  PhotoHistory history_;
  RecentPreviews previews_;
  std::vector<ImmichPhoto> pending_;
};

}  // namespace espframe::native
