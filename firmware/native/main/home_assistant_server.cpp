#include "home_assistant_server.h"

#include "esphome_socket.h"
#include "firmware_update.h"
#include "home_assistant_key.h"
#include "native_network.h"
#include "screen_control.h"
#include "settings.h"
#include "slideshow_interval.h"

#include "bootloader_random.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "sodium/randombytes.h"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

extern "C" void noise_rand_bytes(void *output, size_t size) {
  // The P4 uses an external C6 radio and this firmware leaves the P4 ADC
  // unused. Enable its hardware entropy source for each Noise handshake.
  bootloader_random_enable();
  esp_fill_random(output, size);
  bootloader_random_disable();
}

namespace espframe::native {
namespace {

constexpr char tag[] = "home-assistant";
constexpr char service[] = "_esphomelib";
constexpr uint16_t port = 6053;
constexpr int64_t diagnostic_interval_us = 60'000'000;

const randombytes_implementation hardware_random{
    .implementation_name = []() -> const char * { return "esp32p4"; },
    .random = []() -> uint32_t {
      uint32_t value;
      noise_rand_bytes(&value, sizeof(value));
      return value;
    },
    .stir = nullptr,
    .uniform = nullptr,
    .buf = noise_rand_bytes,
    .close = nullptr,
};

std::optional<float> free_memory_percent(uint32_t capabilities) {
  const auto total = heap_caps_get_total_size(capabilities);
  if (!total)
    return {};
  return 100.0f * heap_caps_get_free_size(capabilities) / total;
}

struct Socket {
  const int fd;
  ~Socket() {
    if (fd >= 0)
      close(fd);
  }
};

struct StationConnection {
  const NativeNetwork &network;
  uint32_t lease;

  static bool current(void *context) {
    const auto &connection = *static_cast<StationConnection *>(context);
    return connection.network.station_lease() == connection.lease;
  }
};

struct HomeAssistantServer {
  const NativeNetwork *network{};
  EspHomeIdentity identity{};
  HomeAssistantKey key{};
  std::string_view paired_key;
  mutable std::optional<int8_t> last_rssi{};
  mutable int64_t next_wifi_sample_us{};
  mutable EspHomeMemorySample last_memory{};
  mutable int64_t next_memory_sample_us{};

  std::optional<int8_t> wifi_rssi() const {
    const auto now = esp_timer_get_time();
    if (now >= next_wifi_sample_us) {
      wifi_ap_record_t ap{};
      last_rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? std::optional<int8_t>{ap.rssi} : std::nullopt;
      next_wifi_sample_us = now + diagnostic_interval_us;
    }
    return last_rssi;
  }

  EspHomeMemorySample memory() const {
    const auto now = esp_timer_get_time();
    if (now >= next_memory_sample_us) {
      // Match the stock diagnostics. The largest blocks are byte-addressable.
      last_memory = {
          free_memory_percent(MALLOC_CAP_INTERNAL),
          static_cast<float>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)),
          free_memory_percent(MALLOC_CAP_SPIRAM),
          static_cast<float>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM)),
      };
      next_memory_sample_us = now + diagnostic_interval_us;
    }
    return last_memory;
  }

  void listen_on_station(uint32_t lease) const {
    const Socket listener{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
    const int reuse = 1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = network->address().ipv4;
    if (listener.fd < 0 || address.sin_addr.s_addr == 0 || network->station_lease() != lease ||
        setsockopt(listener.fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0 ||
        fcntl(listener.fd, F_SETFL, O_NONBLOCK) != 0 ||
        bind(listener.fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
        listen(listener.fd, 2) != 0) {
      ESP_LOGW(tag, "Could not start the ESPHome API listener");
      return;
    }

    const auto mac = esphome_mac_text(identity.mac);
    mdns_txt_item_t discovery[] = {
        {"mac", mac.data()}, {"friendly_name", identity.friendly_name.data()}, {"platform", "ESP32"},
        {"network", "wifi"}, {"api_encryption", esphome_noise_protocol},
    };
    const bool advertised = mdns_service_add(identity.name.data(), service, "_tcp", port,
                                             discovery, sizeof(discovery) / sizeof(discovery[0])) == ESP_OK;
    if (!advertised)
      ESP_LOGW(tag, "ESPHome API discovery is unavailable");

    std::array<char, INET_ADDRSTRLEN> station_address{};
    inet_ntop(AF_INET, &address.sin_addr, station_address.data(), station_address.size());
    next_wifi_sample_us = 0;
    next_memory_sample_us = 0;
    const EspHomeControls controls{
        screen_control, this,
        [](const void *) { return active_slideshow_interval.load(std::memory_order_relaxed); },
        [](const void *context, uint32_t seconds) {
          const auto &server = *static_cast<const HomeAssistantServer *>(context);
          return Settings::save_slide_interval(seconds, server.paired_key);
        },
        [](const void *) { return active_photo_preset.load(std::memory_order_relaxed); },
        [](const void *context, PhotoPreset preset) {
          const auto &server = *static_cast<const HomeAssistantServer *>(context);
          return Settings::save_photo_preset(preset, server.paired_key);
        },
        station_address.data(),
        [](const void *context) { return static_cast<const HomeAssistantServer *>(context)->wifi_rssi(); },
        last_reset_reason(),
        [](const void *) { request_frame_restart(); },
        [](const void *context, uint32_t seconds) {
          const auto &server = *static_cast<const HomeAssistantServer *>(context);
          return Settings::save_connection_timeout(seconds, server.paired_key);
        },
        [](const void *context, uint8_t language) {
          const auto &server = *static_cast<const HomeAssistantServer *>(context);
          return Settings::save_device_language(language, server.paired_key);
        },
        [](const void *context) { return static_cast<const HomeAssistantServer *>(context)->memory(); },
    };
    while (network->station_lease() == lease) {
      const Socket client{accept(listener.fd, nullptr, nullptr)};
      if (client.fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
          break;
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      StationConnection connection{*network, lease};
      serve_esphome_socket(client.fd, identity, key, controls, &connection, StationConnection::current);
    }
    if (advertised)
      mdns_service_remove(service, "_tcp");
  }

  static void run(void *context) {
    const auto &server = *static_cast<HomeAssistantServer *>(context);
    while (true) {
      if (const auto lease = server.network->station_lease())
        server.listen_on_station(lease);
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
};

}  // namespace

bool start_home_assistant(const NativeNetwork &network, EspHomeIdentity identity, std::string_view key) {
  const auto decoded = decode_home_assistant_key(key);
  if (!decoded || !valid_frame_mac(identity.mac) || identity.name.empty() || identity.name.size() > 31)
    return false;
  // Noise initializes libsodium before the first handshake. Register the P4
  // entropy source so that initialization does not try to open /dev/urandom.
  static const bool entropy_ready = randombytes_set_implementation(&hardware_random) == 0;
  if (!entropy_ready)
    return false;
  static HomeAssistantServer server;
  server = {&network, identity, *decoded, key};
  return xTaskCreatePinnedToCore(HomeAssistantServer::run, tag, 6144, &server, 1, nullptr, 0) == pdPASS;
}

}  // namespace espframe::native
