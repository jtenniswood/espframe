#include "native_network.h"

#include "live_trial.h"
#include "native_wireguard.h"
#include "ntp_server_names.h"
#include "settings.h"
#include "station_retry.h"
#include "web_settings.h"

#if !SWIPE_ESPHOME_TRIAL
#include "home_assistant_server.h"
#endif

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace espframe::native {
namespace {

constexpr const char *tag = "network";
constexpr uint32_t got_address = BIT0;
constexpr uint32_t disconnected = BIT1;
constexpr uint32_t address_lost = BIT2;
constexpr uint32_t station_stopped = BIT3;
constexpr uint32_t command_ready = BIT4;
constexpr uint32_t time_sync_requested = BIT5;
#if SWIPE_TRIAL_WIFI_OUTAGE
constexpr uint32_t trial_outage_requested = BIT6;
#endif
constexpr int network_core = 0;
constexpr bool trial_immich = SWIPE_TRIAL_IMMICH;
constexpr bool trial_benchmark = SWIPE_TRIAL_BENCHMARK;

FrameMac read_frame_mac() {
  FrameMac mac{};
  if (esp_read_mac(mac.data(), ESP_MAC_EFUSE_CUSTOM) == ESP_OK && valid_frame_mac(mac))
    return mac;
  if (esp_read_mac(mac.data(), ESP_MAC_EFUSE_FACTORY) == ESP_OK && valid_frame_mac(mac))
    return mac;
  return {};
}

}  // namespace

void NativeNetwork::start(const Settings &settings) {
  settings_ = &settings;
  photo_needs_time_.store(settings.immich_url.starts_with("https://"), std::memory_order_relaxed);
  if constexpr (!trial_immich)
    web_status_.store(WebStatus::Starting, std::memory_order_relaxed);
  if (xTaskCreatePinnedToCore(run_, "native-network", 6144, this, 2, nullptr, network_core) != pdPASS)
    ESP_LOGW(tag, "Could not allocate the Wi-Fi task");
}

NativeNetwork::Address NativeNetwork::address() const {
  return {ipv4_.load(std::memory_order_acquire), web_status_.load(std::memory_order_acquire)};
}

bool NativeNetwork::wireguard_restart_required(const Settings &saved) const {
  return settings_->wireguard != saved.wireguard;
}

FrameIdentity NativeNetwork::identity_for(const std::string &name) const {
  const bool stock_name = trial_immich || (settings_ && !settings_->home_assistant_key.empty());
  return make_frame_identity(name, frame_mac_, NATIVE_HOSTNAME, NATIVE_FRIENDLY_NAME, stock_name);
}

uint32_t NativeNetwork::station_lease() const {
  const uint32_t lease = lease_version_.load(std::memory_order_acquire);
  return has_station_address(lease) && !provisioning_.load(std::memory_order_acquire) ? lease : 0;
}

uint32_t NativeNetwork::photo_connection() const {
  const bool ready = !photo_needs_time_.load(std::memory_order_relaxed) ||
                     photo_time_ready_.load(std::memory_order_acquire);
  return ready ? station_lease() : 0;
}

void NativeNetwork::use_photo_connection(const PhotoConnectionSettings &connection) {
  const bool https = connection.immich_url.starts_with("https://");
  photo_needs_time_.store(https, std::memory_order_relaxed);
  set_wireguard_photo_url(connection.immich_url);
  if (https || connection.prepared_slides_need_time())
    request_time_sync();
}

void NativeNetwork::request_time_sync() {
  // Latch the request even if the Wi-Fi task is still starting.
  time_sync_requested_.store(true, std::memory_order_release);
  if (available())
    xTaskNotify(task_, time_sync_requested, eSetBits);
}

#if SWIPE_TRIAL_WIFI_OUTAGE
void NativeNetwork::start_trial_wifi_outage() {
  auto ready = TrialLink::Ready;
  if (station_lease() && trial_link_.compare_exchange_strong(ready, TrialLink::Requested))
    xTaskNotify(task_, trial_outage_requested, eSetBits);
}
#endif

bool NativeNetwork::submit(Request request, const WifiCredentials &wifi) {
  bool idle = false;
  if (!available() || !busy_.compare_exchange_strong(idle, true))
    return false;
  const auto recovery = recovery_status();
  if (request == Request::Recover) {
    if (recovery == RecoveryStatus::Off || address().ipv4) {
      busy_.store(false, std::memory_order_release);
      return false;
    }
    recovery_status_.store(RecoveryStatus::Joining, std::memory_order_release);
  }
  const Command command{request, wifi};
  if (xQueueSend(commands_, &command, 0) != pdPASS) {
    if (request == Request::Recover)
      recovery_status_.store(recovery, std::memory_order_release);
    busy_.store(false, std::memory_order_release);
    return false;
  }
  xTaskNotify(task_, command_ready, eSetBits);
  return true;
}

bool NativeNetwork::submit_recovery(const WifiCredentials &wifi) { return submit(Request::Recover, wifi); }

bool NativeNetwork::poll(Reply &reply) {
  return available() && xQueueReceive(replies_, &reply, 0) == pdPASS;
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
  } else if (base == WIFI_EVENT && (event == WIFI_EVENT_STA_DISCONNECTED || event == WIFI_EVENT_STA_STOP)) {
    network.ipv4_.store(0, std::memory_order_release);
    after = next_station_lease(before, false);
    notification = event == WIFI_EVENT_STA_STOP ? station_stopped : disconnected;
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

bool NativeNetwork::init_station_(const WifiCredentials &wifi) {
  if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK)
    return false;
  station_ = esp_netif_create_default_wifi_sta();
  if (station_ == nullptr)
    return false;
  if constexpr (trial_benchmark) {
    esp_netif_set_hostname(station_, "espframe-native");
  } else {
    frame_mac_ = read_frame_mac();
    identity_ = identity_for(settings_->frame_name);
    if (esp_netif_set_hostname(station_, identity_.hostname.c_str()) != ESP_OK)
      ESP_LOGW(tag, "Could not set the frame's DHCP hostname");
  }

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  init.nvs_enable = 0;
  if (esp_wifi_init(&init) != ESP_OK || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
      esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, station_event_, this) != ESP_OK ||
      esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, station_event_, this) != ESP_OK)
    return false;

  if constexpr (!trial_immich) {
    hotspot_configured_ = prepare_recovery_();
    if (!hotspot_configured_)
      ESP_LOGW(tag, "Could not configure the Wi-Fi setup hotspot");
  }
  if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK)
    return false;
  if constexpr (!trial_immich) {
    if (mdns_init() != ESP_OK) {
      ESP_LOGW(tag, "Could not start local name discovery");
    } else if (mdns_hostname_set(identity_.hostname.c_str()) != ESP_OK) {
      ESP_LOGW(tag, "Could not register the frame's local name");
      mdns_free();
    }
  }
  station_config_valid_ = start_station_(wifi);
  return station_config_valid_ || (wifi.configured() && start_station_({}));
}

bool NativeNetwork::start_station_(const WifiCredentials &wifi) {
  if (recovery_status() != RecoveryStatus::Off) {
    // Switching from AP to AP+STA starts the station without cycling the
    // hotspot. Its client may still have to follow the home network's channel.
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK)
      return false;
    station_running_ = true;
  }
  if (wifi.configured()) {
    wifi_config_t station{};
    std::memcpy(station.sta.ssid, wifi.ssid.data(), sizeof(station.sta.ssid));
    std::memcpy(station.sta.password, wifi.password.data(), sizeof(station.sta.password));
    station.sta.threshold.authmode = wifi.password.front() == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    // A mesh can advertise the same network on several channels.
    station.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    station.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    if (esp_wifi_set_config(WIFI_IF_STA, &station) != ESP_OK)
      return false;
  }
  if (!station_running_)
    station_running_ = esp_wifi_start() == ESP_OK;
  if (station_running_) {
    // The frame is mains-powered. Match ESPHome's receive policy so
    // photo packets do not wait for the access point's DTIM interval.
    if (const auto error = esp_wifi_set_ps(WIFI_PS_NONE); error != ESP_OK)
      ESP_LOGW(tag, "Could not disable Wi-Fi modem sleep: %s", esp_err_to_name(error));
  }
  return station_running_;
}

bool NativeNetwork::replace_station_(const WifiCredentials &wifi) {
  if (station_running_) {
    const auto stopped = recovery_status() != RecoveryStatus::Off ? esp_wifi_set_mode(WIFI_MODE_AP) : esp_wifi_stop();
    if (stopped != ESP_OK)
      return false;
    station_running_ = false;
    // ESP-NETIF handles STA_STOP before this callback. Wait for it so an
    // old DHCP result cannot acknowledge the new credentials.
    const auto deadline = esp_timer_get_time() + 3'000'000;
    while (esp_timer_get_time() < deadline) {
      uint32_t events = 0;
      xTaskNotifyWait(0, UINT32_MAX, &events, pdMS_TO_TICKS(100));
      if (events & station_stopped)
        return start_station_(wifi);
    }
    return false;
  }
  return start_station_(wifi);
}

bool NativeNetwork::prepare_recovery_() {
  uint8_t mac[6]{};
  if (esp_wifi_get_mac(WIFI_IF_AP, mac) != ESP_OK || esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK)
    return false;
  std::snprintf(recovery_network_.ssid.data(), recovery_network_.ssid.size(), "espframe_%02X%02X%02X",
                static_cast<unsigned>(mac[3]), static_cast<unsigned>(mac[4]), static_cast<unsigned>(mac[5]));
  // ESP-IDF leaves the P4 SAR ADC random source enabled at application startup.
  constexpr char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  static_assert(sizeof(alphabet) == 33);
  esp_fill_random(recovery_network_.password.data(), recovery_network_.password.size() - 1);
  for (size_t i = 0; i + 1 < recovery_network_.password.size(); ++i)
    recovery_network_.password[i] = alphabet[static_cast<uint8_t>(recovery_network_.password[i]) & 31u];
  wifi_config_t config{};
  std::memcpy(config.ap.ssid, recovery_network_.ssid.data(), recovery_network_.ssid.size());
  std::memcpy(config.ap.password, recovery_network_.password.data(), recovery_network_.password.size());
  config.ap.ssid_len = std::strlen(recovery_network_.ssid.data());
  config.ap.channel = 1;
  config.ap.max_connection = 2;
  config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  // Set WPA2 credentials before the radio can advertise the hotspot.
  return esp_wifi_set_config(WIFI_IF_AP, &config) == ESP_OK;
}

bool NativeNetwork::start_recovery_() {
  if (!hotspot_configured_)
    return false;
  if (!hotspot_)
    hotspot_ = esp_netif_create_default_wifi_ap();
  if (!hotspot_ || !start_web_settings(*settings_, *this) || esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK)
    return false;
  if (!station_running_) {
    if (esp_wifi_start() != ESP_OK) {
      esp_wifi_set_mode(WIFI_MODE_STA);
      return false;
    }
    station_running_ = true;
  }
  const int64_t deadline = esp_timer_get_time() + 2'000'000;
  while (!esp_netif_is_netif_up(hotspot_)) {
    if (esp_timer_get_time() >= deadline) {
      esp_wifi_set_mode(WIFI_MODE_STA);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(25));
  }
  recovery_status_.store(RecoveryStatus::Ready, std::memory_order_release);
  ESP_LOGI(tag, "Wi-Fi setup hotspot %s ready at %s", recovery_network_.ssid.data(), recovery_url);
  return true;
}

bool NativeNetwork::stop_recovery_() {
  if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK)
    return false;
  recovery_status_.store(RecoveryStatus::Off, std::memory_order_release);
  return true;
}

bool NativeNetwork::try_provision_(const WifiCredentials &wifi) {
  if (!replace_station_(wifi) || esp_wifi_connect() != ESP_OK)
    return false;
  const auto deadline = esp_timer_get_time() + 18'000'000;
  while (esp_timer_get_time() < deadline) {
    uint32_t events = 0;
    xTaskNotifyWait(0, UINT32_MAX, &events, pdMS_TO_TICKS(100));
    const auto lease = lease_version_.load(std::memory_order_acquire);
    if (has_station_address(lease)) {
      wifi_ap_record_t ap{};
      if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        const auto *name = reinterpret_cast<const char *>(ap.ssid);
        if (std::string_view(name, strnlen(name, sizeof(ap.ssid))) == wifi.ssid.data() &&
            lease_version_.load(std::memory_order_acquire) == lease)
          return Settings::save_wifi(wifi, settings_->reset_epoch);
      }
    }
    if (events & disconnected)
      return false;
  }
  return false;
}

void NativeNetwork::scan_(Reply &reply) {
  if (!station_running_ || esp_wifi_scan_start(nullptr, true) != ESP_OK)
    return;
  std::array<wifi_ap_record_t, 12> records{};
  uint16_t count = records.size();
  if (esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK) {
    esp_wifi_clear_ap_list();
    return;
  }
  for (uint16_t index = 0; index < count && reply.count < reply.networks.size(); ++index) {
    const auto &record = records[index];
    if (record.ssid[0] == 0)
      continue;
    auto &result = reply.networks[reply.count++];
    std::memcpy(result.ssid.data(), record.ssid, result.ssid.size() - 1);
    result.rssi = record.rssi;
    result.secured = record.authmode != WIFI_AUTH_OPEN;
  }
  reply.success = true;
}

void NativeNetwork::handle_(const Command &command, WifiCredentials &saved) {
  Reply reply{command.request};
  if (command.request == Request::Scan) {
    scan_(reply);
  } else {
    provisioning_.store(true, std::memory_order_release);
    reply.success = try_provision_(command.wifi);
    if (reply.success) {
      saved = command.wifi;
      station_config_valid_ = true;
    } else {
      station_config_valid_ = replace_station_(saved);
      if (!station_config_valid_)
        ESP_LOGW(tag, "Could not restore the saved Wi-Fi station");
    }
    provisioning_.store(false, std::memory_order_release);
  }
  if (command.request == Request::Recover)
    recovery_status_.store(reply.success ? RecoveryStatus::Saved : RecoveryStatus::Failed, std::memory_order_release);
  else
    xQueueOverwrite(replies_, &reply);
  busy_.store(false, std::memory_order_release);
}

void NativeNetwork::connect_loop_(WifiCredentials saved) {
  const auto &configured_servers = settings_->clock.ntp_servers;
  auto ntp_servers = espframe_normalize_ntp_servers(configured_servers[0], configured_servers[1], configured_servers[2]);
  uint32_t ntp_revision = 0;
  const bool https = settings_->immich_url.starts_with("https://");
  const bool wireguard_enabled = !SWIPE_ESPHOME_TRIAL && configure_wireguard(settings_->wireguard, settings_->immich_url);
  const bool need_time_at_boot = https || wireguard_enabled || settings_->prepared_slides_need_time() || settings_->clock.show ||
                                 settings_->schedule.enabled || settings_->brightness_day != settings_->night_brightness() ||
                                 settings_->tone.needs_time() || settings_->needs_local_date();
  bool sntp_started = false;
  bool web_started = false;
#if !SWIPE_ESPHOME_TRIAL
  bool assistant_started = settings_->home_assistant_key.empty() || !valid_frame_mac(frame_mac_);
#endif
  uint32_t events = 0;
  uint32_t last_lease = 0;
  StationRetry retry;
  RecoveryWindow recovery;
  int64_t next_hotspot_attempt = 0;
#if SWIPE_TRIAL_WIFI_OUTAGE
  int64_t trial_deadline = 0;
#endif
  photo_time_ready_.store(clock_time_is_valid(std::time(nullptr)), std::memory_order_release);

  while (true) {
    if constexpr (!SWIPE_ESPHOME_TRIAL) {
      if (const auto updated = updated_ntp_servers(ntp_revision)) {
        auto servers = espframe_normalize_ntp_servers((*updated)[0], (*updated)[1], (*updated)[2]);
        if (servers != ntp_servers) {
          if (sntp_started)
            esp_netif_sntp_deinit();
          sntp_started = false;
          ntp_servers = std::move(servers);
        }
      }
    }
    if constexpr (!trial_immich) {
      Command command{};
      if (xQueueReceive(commands_, &command, 0) == pdPASS) {
        handle_(command, saved);
        if (command.request != Request::Scan) {
          retry = {};
          events = 0;
        }
      }
    }
    const int64_t now = esp_timer_get_time();
    const auto lease = lease_version_.load(std::memory_order_acquire);
    const bool connected = has_station_address(lease);
    bool hold_reconnect = false;
#if SWIPE_TRIAL_WIFI_OUTAGE
    auto phase = trial_link();
    if (phase == TrialLink::Requested) {
      phase = connected && esp_wifi_disconnect() == ESP_OK ? TrialLink::Disconnecting : TrialLink::Failed;
      trial_deadline = now + 2'000'000;
    } else if (phase == TrialLink::Disconnecting && !connected && (events & disconnected)) {
      phase = TrialLink::Offline;
      trial_deadline = now + 12'000'000;
    } else if (phase == TrialLink::Disconnecting && now >= trial_deadline) {
      phase = TrialLink::Failed;
    } else if (phase == TrialLink::Offline && connected) {
      phase = TrialLink::Failed;
    } else if (phase == TrialLink::Offline && now >= trial_deadline) {
      phase = TrialLink::Reconnecting;
    } else if (phase == TrialLink::Reconnecting && connected) {
      phase = TrialLink::Reconnected;
    }
    // Only the station task changes the phase after it accepts the request.
    if (phase != TrialLink::Ready)
      trial_link_.store(phase, std::memory_order_release);
    hold_reconnect = phase == TrialLink::Disconnecting || phase == TrialLink::Offline;
#endif
    recovery.update(now, connected);
    bool hotspot = recovery_status() != RecoveryStatus::Off;
    if constexpr (!trial_immich) {
      if (const auto change = recovery.change_at(saved.configured(), hotspot);
          hotspot_configured_ && change && now >= std::max(*change, next_hotspot_attempt)) {
        if (hotspot ? stop_recovery_() : start_recovery_()) {
          hotspot = !hotspot;
          next_hotspot_attempt = 0;
        } else {
          next_hotspot_attempt = now + 5'000'000;
          ESP_LOGW(tag, "Could not %s the Wi-Fi setup hotspot", hotspot ? "stop" : "start");
        }
      }
    }
    const auto event = (events & address_lost) ? StationRetry::Event::AddressLost
                       : (events & disconnected) ? StationRetry::Event::Disconnected : StationRetry::Event::None;
    const bool auto_reconnect = !hold_reconnect && saved.configured() && station_running_ && station_config_valid_;
    switch (auto_reconnect ? retry.step(now, connected, event) : StationRetry::Action::Wait) {
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
        if constexpr (trial_immich) {
          wifi_ps_type_t mode{};
          const int power_save = esp_wifi_get_ps(&mode) == ESP_OK ? static_cast<int>(mode) : -1;
          wifi_ap_record_t ap{};
          const bool known_ap = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
          record_live_trial_wifi_link(power_save, known_ap ? ap.rssi : -128, known_ap ? ap.primary : 0);
        }
      }
      if (!web_started) {
        if constexpr (trial_immich) {
          web_started = start_live_trial_status(*this);
        } else {
          web_started = start_web_settings(*settings_, *this);
          if (web_started)
            web_status_.store(WebStatus::Ready, std::memory_order_release);
        }
      }
#if !SWIPE_ESPHOME_TRIAL
      if (!assistant_started)
        assistant_started = start_home_assistant(*this,
            {identity_.hostname, identity_.friendly_name, "Guition JC8012P4A1", frame_mac_},
            settings_->home_assistant_key);
#endif
      const bool need_time = need_time_at_boot || time_sync_requested_.load(std::memory_order_acquire);
      if (need_time && !sntp_started) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(0, ESP_SNTP_SERVER_LIST());
        static_assert(std::size(config.servers) >= ESPFRAME_NTP_SERVER_COUNT);
        for (const auto &server : ntp_servers) {
          if (!server.empty())
            config.servers[config.num_of_servers++] = server.c_str();
        }
        sntp_started = esp_netif_sntp_init(&config) == ESP_OK;
        if (sntp_started)
          ESP_LOGI(tag, "Time sync configured (%u servers)", static_cast<unsigned>(config.num_of_servers));
      }
      const bool clock_ready = clock_time_is_valid(std::time(nullptr));
      photo_time_ready_.store(clock_ready, std::memory_order_release);
      const bool time_ready = !photo_needs_time_.load(std::memory_order_relaxed) || clock_ready;
      const bool services_ready = web_started && (!need_time || sntp_started);
      wait = services_ready ? (time_ready ? portMAX_DELAY : pdMS_TO_TICKS(250)) : pdMS_TO_TICKS(2000);
#if !SWIPE_ESPHOME_TRIAL
      if (!assistant_started)
        wait = std::min(wait, pdMS_TO_TICKS(2000));
#endif
    } else {
      if (has_station_address(last_lease))
        ESP_LOGI(tag, "Wi-Fi disconnected; reconnecting in the background");
      if (auto_reconnect) {
        const auto remaining_us = retry.next_at_us() - esp_timer_get_time();
        wait = pdMS_TO_TICKS(std::max<int64_t>(1, (remaining_us + 999) / 1000));
      } else {
        wait = portMAX_DELAY;
      }
    }
    if (const auto change = recovery.change_at(saved.configured(), hotspot); hotspot_configured_ && change) {
      const auto remaining_us = std::max(*change, next_hotspot_attempt) - esp_timer_get_time();
      wait = std::min(wait, pdMS_TO_TICKS(std::max<int64_t>(1, (remaining_us + 999) / 1000)));
    }
#if SWIPE_TRIAL_WIFI_OUTAGE
    if (hold_reconnect) {
      const auto remaining_us = trial_deadline - esp_timer_get_time();
      wait = std::min(wait, pdMS_TO_TICKS(std::max<int64_t>(1, (remaining_us + 999) / 1000)));
    }
#endif
    if (wireguard_enabled) {
      const bool ready = connected && !hotspot && clock_time_is_valid(std::time(nullptr));
      poll_wireguard(ready ? station_lease() : 0);
      wait = std::min(wait, pdMS_TO_TICKS(2000));
    }
    last_lease = lease;
    events = 0;
    xTaskNotifyWait(0, UINT32_MAX, &events, wait);
  }
}

void NativeNetwork::run_(void *context) {
  auto &network = *static_cast<NativeNetwork *>(context);
  network.task_ = xTaskGetCurrentTaskHandle();
  if constexpr (!trial_immich) {
    network.commands_ = xQueueCreate(1, sizeof(Command));
    network.replies_ = xQueueCreate(1, sizeof(Reply));
  }
  const auto wifi = WifiCredentials::from(network.settings_->ssid, network.settings_->password).value_or(WifiCredentials{});
  if ((trial_immich || (network.commands_ && network.replies_)) && network.init_station_(wifi)) {
    network.available_.store(true, std::memory_order_release);
    network.connect_loop_(wifi);
  }
  ESP_LOGW(tag, "Could not initialize Guition Wi-Fi");
  // Keep the callback target valid even if driver initialization stopped
  // after installing event handlers.
  vTaskSuspend(nullptr);
}

}  // namespace espframe::native
