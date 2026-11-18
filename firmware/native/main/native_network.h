#pragma once

#include "esp_event.h"
#include "esp_netif_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <atomic>
#include <cstdint>

namespace espframe::native {

struct Settings;

class NativeNetwork {
 public:
  enum class WebStatus : uint8_t { Disabled, Starting, Ready };
  struct Address {
    uint32_t ipv4;
    WebStatus web;
  };

  void start(const Settings &settings);
  Address address() const;
  uint32_t photo_connection() const;

 private:
  bool init_station_();
  void connect_loop_();
  static void run_(void *context);
  static void station_event_(void *argument, esp_event_base_t base, int32_t event, void *data);

  const Settings *settings_{};
  esp_netif_t *station_{};
  TaskHandle_t task_{};
  std::atomic<uint32_t> lease_version_{};
  std::atomic<uint32_t> ipv4_{};
  std::atomic<WebStatus> web_status_{WebStatus::Disabled};
  std::atomic<bool> photo_time_ready_{};
};

}  // namespace espframe::native
