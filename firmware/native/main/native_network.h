#pragma once

#include "wifi_credentials.h"

#include "esp_event.h"
#include "esp_netif_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
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
  enum class Request : uint8_t { Provision, Scan };
  struct AccessPoint {
    std::array<char, 33> ssid{};
    int8_t rssi{};
    bool secured{};
  };
  struct Reply {
    Request request;
    bool success{};
    uint8_t count{};
    std::array<AccessPoint, 12> networks{};
  };

  void start(const Settings &settings);
  Address address() const;
  uint32_t photo_connection() const;
  bool available() const { return available_.load(std::memory_order_acquire); }
  bool submit(Request request, const WifiCredentials &wifi = {});
  bool poll(Reply &reply);

 private:
  struct Command {
    Request request;
    WifiCredentials wifi;
  };
  bool init_station_(const WifiCredentials &wifi);
  bool start_station_(const WifiCredentials &wifi);
  bool replace_station_(const WifiCredentials &wifi);
  bool try_provision_(const WifiCredentials &wifi);
  void handle_(const Command &command, WifiCredentials &saved);
  void scan_(Reply &reply);
  void connect_loop_(WifiCredentials saved);
  static void run_(void *context);
  static void station_event_(void *argument, esp_event_base_t base, int32_t event, void *data);

  const Settings *settings_{};
  esp_netif_t *station_{};
  TaskHandle_t task_{};
  QueueHandle_t commands_{};
  QueueHandle_t replies_{};
  bool station_running_{};
  std::atomic<uint32_t> lease_version_{};
  std::atomic<uint32_t> ipv4_{};
  std::atomic<WebStatus> web_status_{WebStatus::Disabled};
  std::atomic<bool> photo_time_ready_{};
  std::atomic<bool> available_{};
  std::atomic<bool> provisioning_{};
};

}  // namespace espframe::native
