#pragma once

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
  static constexpr int slide_limit = 2'000;

  bool read_settings();
  bool prepare(int slide, uint16_t *panel);
  uint16_t slide_seconds() const { return this->settings_.slide_seconds; }

 private:
  using AssetId = std::array<char, 37>;
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
  std::vector<AssetId> history_;
  std::vector<AssetId> pending_;
};

}  // namespace espframe::native
