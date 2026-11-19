#include "native_network.h"

#include "settings.h"
#include "station_retry.h"
#include "web_settings.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include <algorithm>
#include <cstring>
#include <ctime>

namespace espframe::native {
namespace {

constexpr const char *tag = "network";
constexpr uint32_t got_address = BIT0;
constexpr uint32_t disconnected = BIT1;
constexpr uint32_t address_lost = BIT2;
constexpr int network_core = 0;

}  // namespace

void NativeNetwork::start(const Settings &settings) {
  settings_ = &settings;
  if (!settings.web_password.empty())
    web_status_.store(WebStatus::Starting, std::memory_order_relaxed);
  if (xTaskCreatePinnedToCore(run_, "native-network", 4096, this, 2, nullptr, network_core) != pdPASS)
    ESP_LOGW(tag, "Could not allocate the Wi-Fi task");
}

NativeNetwork::Address NativeNetwork::address() const {
  return {ipv4_.load(std::memory_order_acquire), web_status_.load(std::memory_order_acquire)};
}

uint32_t NativeNetwork::photo_connection() const {
  const uint32_t lease = lease_version_.load(std::memory_order_acquire);
  return has_station_address(lease) && photo_time_ready_.load(std::memory_order_acquire) ? lease : 0;
}

void NativeNetwork::station_event_(void *argument, esp_event_base_t base, int32_t event, void *data) {
  auto &network = *static_cast<NativeNetwork *>(argument);
  const uint32_t before = network.lease_version_.load(std::memory_order_relaxed);
  uint32_t notification = 0;
  uint32_t after = before;
  if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
    const auto &ip = *static_cast<const ip_event_got_ip_t *>(data);
    if (ip.esp_netif != network.station_)
      return;
    network.ipv4_.store(ip.ip_info.ip.addr, std::memory_order_release);
    after = next_station_lease(before, true, ip.ip_changed);
    notification = got_address;
  } else if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
    network.ipv4_.store(0, std::memory_order_release);
    after = next_station_lease(before, false);
    notification = disconnected;
  } else if (base == IP_EVENT && event == IP_EVENT_STA_LOST_IP && has_station_address(before)) {
    network.ipv4_.store(0, std::memory_order_release);
    after = next_station_lease(before, false);
    notification = address_lost;
  }
  if (notification) {
    network.lease_version_.store(after, std::memory_order_release);
    xTaskNotify(network.task_, notification, eSetBits);
  }
}

bool NativeNetwork::init_station_() {
  if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK)
    return false;
  station_ = esp_netif_create_default_wifi_sta();
  if (station_ == nullptr)
    return false;
  esp_netif_set_hostname(station_, "espframe-native");

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  init.nvs_enable = 0;
  if (esp_wifi_init(&init) != ESP_OK || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
      esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, station_event_, this) != ESP_OK ||
      esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, station_event_, this) != ESP_OK)
    return false;

  wifi_config_t station{};
  std::memcpy(station.sta.ssid, settings_->ssid.data(), settings_->ssid.size());
  std::memcpy(station.sta.password, settings_->password.data(), settings_->password.size());
  station.sta.threshold.authmode = settings_->password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  return esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK &&
         esp_wifi_set_config(WIFI_IF_STA, &station) == ESP_OK && esp_wifi_start() == ESP_OK;
}

void NativeNetwork::connect_loop_() {
  const bool https = settings_->immich_url.starts_with("https://");
  const bool need_time = https || settings_->clock.show || settings_->schedule.enabled ||
                         settings_->brightness_day != settings_->night_brightness();
  bool sntp_started = !need_time;
  bool web_started = false;
  uint32_t events = 0;
  uint32_t last_lease = 0;
  StationRetry retry;
  photo_time_ready_.store(!https || clock_time_is_valid(std::time(nullptr)), std::memory_order_release);

  while (true) {
    const int64_t now = esp_timer_get_time();
    const auto lease = lease_version_.load(std::memory_order_acquire);
    const bool connected = has_station_address(lease);
    const auto event = (events & address_lost) ? StationRetry::Event::AddressLost
                       : (events & disconnected) ? StationRetry::Event::Disconnected : StationRetry::Event::None;
    switch (retry.step(now, connected, event)) {
      case StationRetry::Action::Connect:
        if (esp_wifi_connect() != ESP_OK)
          retry.failed(esp_timer_get_time());
        break;
      case StationRetry::Action::Disconnect:
        if (!has_station_address(lease_version_.load(std::memory_order_acquire)))
          esp_wifi_disconnect();
        break;
      case StationRetry::Action::Wait:
        break;
    }

    TickType_t wait;
    if (connected) {
      if (lease != last_lease) {
        esp_netif_ip_info_t ip{};
        if (esp_netif_get_ip_info(station_, &ip) == ESP_OK)
          ESP_LOGI(tag, "Wi-Fi connected at " IPSTR, IP2STR(&ip.ip));
      }
      if (!web_started) {
        web_started = start_web_settings(*settings_);
        if (web_started && !settings_->web_password.empty())
          web_status_.store(WebStatus::Ready, std::memory_order_release);
      }
      if (!sntp_started) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        sntp_started = esp_netif_sntp_init(&config) == ESP_OK;
      }
      const bool time_ready = !https || clock_time_is_valid(std::time(nullptr));
      photo_time_ready_.store(time_ready, std::memory_order_release);
      wait = sntp_started && web_started ? (time_ready ? portMAX_DELAY : pdMS_TO_TICKS(250)) : pdMS_TO_TICKS(2000);
    } else {
      if (has_station_address(last_lease))
        ESP_LOGI(tag, "Wi-Fi disconnected; reconnecting in the background");
      const auto remaining_us = retry.next_at_us() - esp_timer_get_time();
      wait = pdMS_TO_TICKS(std::max<int64_t>(1, (remaining_us + 999) / 1000));
    }
    last_lease = lease;
    events = 0;
    xTaskNotifyWait(0, UINT32_MAX, &events, wait);
  }
}

void NativeNetwork::run_(void *context) {
  auto &network = *static_cast<NativeNetwork *>(context);
  network.task_ = xTaskGetCurrentTaskHandle();
  if (network.init_station_())
    network.connect_loop_();
  ESP_LOGW(tag, "Could not initialize Guition Wi-Fi");
  // Keep the callback target valid even if driver initialization stopped
  // after installing event handlers.
  vTaskSuspend(nullptr);
}

}  // namespace espframe::native
