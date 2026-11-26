#pragma once

#include "frame_identity.h"
#include "wifi_credentials.h"
#include "wifi_recovery.h"

#include "esp_event.h"
#include "esp_netif_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <atomic>
#include <cstdint>

namespace espframe::native {

struct Settings;
struct PhotoConnectionSettings;

class NativeNetwork {
 public:
  enum class WebStatus : uint8_t { Disabled, Starting, Ready };
  struct Address {
    uint32_t ipv4;
    WebStatus web;
  };
  enum class Request : uint8_t { Provision, Scan, Recover };
  enum class RecoveryStatus : uint8_t { Off, Ready, Joining, Failed, Saved };
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
  uint32_t station_lease() const;
  uint32_t photo_connection() const;
  bool wireguard_restart_required(const Settings &saved) const;
  void use_photo_connection(const PhotoConnectionSettings &connection);
  void request_time_sync();
#if SWIPE_TRIAL_WIFI_OUTAGE
  enum class TrialLink : uint8_t { Ready, Requested, Disconnecting, Offline, Reconnecting, Reconnected, Failed };
  void start_trial_wifi_outage();
  TrialLink trial_link() const { return trial_link_.load(std::memory_order_acquire); }
#endif
  bool available() const { return available_.load(std::memory_order_acquire); }
  bool busy() const { return busy_.load(std::memory_order_acquire); }
  bool submit(Request request, const WifiCredentials &wifi = {});
  bool poll(Reply &reply);
  RecoveryStatus recovery_status() const { return recovery_status_.load(std::memory_order_acquire); }
  const RecoveryNetwork &recovery_network() const { return recovery_network_; }
  bool submit_recovery(const WifiCredentials &wifi);
  FrameIdentity identity_for(const std::string &name) const;
  const FrameIdentity &running_identity() const { return identity_; }

 private:
  struct Command {
    Request request;
    WifiCredentials wifi;
  };
  bool init_station_(const WifiCredentials &wifi);
  bool start_station_(const WifiCredentials &wifi);
  bool replace_station_(const WifiCredentials &wifi);
  bool prepare_recovery_();
  bool start_recovery_();
  bool stop_recovery_();
  bool try_provision_(const WifiCredentials &wifi);
  void handle_(const Command &command, WifiCredentials &saved);
  void scan_(Reply &reply);
  void connect_loop_(WifiCredentials saved);
  static void run_(void *context);
  static void station_event_(void *argument, esp_event_base_t base, int32_t event, void *data);

  const Settings *settings_{};
  FrameMac frame_mac_{};
  FrameIdentity identity_;
  esp_netif_t *station_{};
  esp_netif_t *hotspot_{};
  TaskHandle_t task_{};
  QueueHandle_t commands_{};
  QueueHandle_t replies_{};
  bool station_running_{};
  bool station_config_valid_{true};
  bool hotspot_configured_{};
  std::atomic<uint32_t> lease_version_{};
  std::atomic<uint32_t> ipv4_{};
  std::atomic<WebStatus> web_status_{WebStatus::Disabled};
  std::atomic<bool> photo_time_ready_{};
  std::atomic<bool> photo_needs_time_{};
  std::atomic<bool> time_sync_requested_{};
  std::atomic<bool> available_{};
  std::atomic<bool> busy_{};
  std::atomic<bool> provisioning_{};
#if SWIPE_TRIAL_WIFI_OUTAGE
  std::atomic<TrialLink> trial_link_{TrialLink::Ready};
#endif
  RecoveryNetwork recovery_network_;
  std::atomic<RecoveryStatus> recovery_status_{RecoveryStatus::Off};
};

}  // namespace espframe::native
