#pragma once

#include "photo_history.h"
#include "recent_previews.h"
#include "settings.h"

#include "esp_event.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace espframe::native {

// Settings are optional. This source is owned by the slide preparation task
// after startup, including the HTTP client and per-session photo order.
class ImmichSource {
 public:
  bool read_settings();
  bool prepare(SlideIndex slide, uint16_t *panel);
  SlideIndex first_slide() const { return this->history_.first(); }
  uint16_t slide_seconds() const { return this->settings_.slide_seconds; }

 private:
  enum class Request { Search, Preview };

  static void wifi_event_(void *argument, esp_event_base_t base, int32_t event, void *);
  bool start_wifi_();
  bool online_();
  int fetch_(Request request, const char *asset, size_t limit, size_t &received);
  bool refill_();
  bool next_asset_(AssetId &id);

  Settings settings_;
  esp_http_client_handle_t http_{};
  EventGroupHandle_t wifi_events_{};
  bool wifi_started_{};
  bool wifi_ready_{};
  bool sntp_started_{};
  uint8_t *encoded_{};
  PhotoHistory history_;
  RecentPreviews previews_;
  std::vector<AssetId> pending_;
};

}  // namespace espframe::native
