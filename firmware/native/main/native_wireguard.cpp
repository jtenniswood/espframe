#include "native_wireguard.h"

#include "clock_settings.h"
#include "settings.h"
#include "wireguard_runtime.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wireguard.h"
#include "lwip/dns.h"
#include "lwip/ip4.h"
#include "lwip/tcpip.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <ctime>
#include <limits>
#include <mutex>

namespace espframe::native {
namespace {

constexpr char tag[] = "wireguard";
constexpr int64_t diagnostics_interval = 30'000'000;
EspframeWireguardConfig settings;
std::string photo_host;
std::array<char, IP4ADDR_STRLEN_MAX> endpoint{};
wireguard_config_t config = ESP_WIREGUARD_CONFIG_DEFAULT();
wireguard_ctx_t context = ESP_WIREGUARD_CONTEXT_DEFAULT();
std::atomic<uint32_t> desired_lease{};
std::atomic<bool> callback_pending{};
uint32_t active_lease{};
bool connected_interface{};
int64_t next_diagnostic{};
int64_t next_attempt{};
std::mutex status_mutex;
WireguardStatus status;

void publish(WireguardStatus next) {
  const std::lock_guard lock(status_mutex);
  status = next;
}

void reset_interface() {
  if (context.netif)
    esp_wireguard_disconnect(&context);
  context = {};
  connected_interface = false;
  next_attempt = 0;
  next_diagnostic = 0;
  publish({.running = true, .connected = false, .handshake_age = {}});
}

err_t resolve_ipv4(const std::string &host, ip_addr_t &address) {
  return dns_gethostbyname_addrtype(host.c_str(), &address, [](const char *, const ip_addr_t *, void *) {},
                                   nullptr, LWIP_DNS_ADDRTYPE_IPV4);
}

void log_photo_route(bool connected) {
  const auto now = esp_timer_get_time();
  if (now < next_diagnostic)
    return;
  next_diagnostic = now + diagnostics_interval;
  std::string host;
  {
    const std::lock_guard lock(status_mutex);
    host = photo_host;
  }
  if (host.empty())
    return;
  ip_addr_t address{};
  if (resolve_ipv4(host, address) != ERR_OK) {
    ESP_LOGI(tag, "Tunnel %s; Immich host %s is not resolved yet", connected ? "up" : "down", host.c_str());
    return;
  }
  const auto *route = ip4_route(ip_2_ip4(&address));
  const auto target = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&address)));
  ESP_LOGI(tag, "Tunnel %s; Immich host %s -> %s via %c%c; %s", connected ? "up" : "down",
           host.c_str(), espframe_format_ipv4(target).c_str(),
           route ? route->name[0] : '-', route ? route->name[1] : '-',
           espframe_wireguard_coverage(settings, target).c_str());
}

bool start_interface() {
  ip_addr_t resolved{};
  if (resolve_ipv4(settings.endpoint, resolved) != ERR_OK ||
      !ip4addr_ntoa_r(ip_2_ip4(&resolved), endpoint.data(), endpoint.size()))
    return false;

  config.endpoint = endpoint.data();
  if (esp_wireguard_init(&config, &context) != ESP_OK)
    return false;
  if (esp_wireguard_connect(&context) != ESP_OK)
    return false;

  // Apply the same local-network priority as the ESPHome frame. This runs
  // on the lwIP thread with all other interface work.
  bool moved = false;
  netif_list = espframe_move_to_tail(netif_list, [](const netif *entry) { return entry == context.netif; }, moved);
  for (const auto &[address, mask] : settings.allowed_ips) {
    if (esp_wireguard_add_allowed_ip(&context, address.c_str(), mask.c_str()) != ESP_OK) {
      ESP_LOGW(tag, "Could not configure the allowed networks");
      return false;
    }
  }
  ESP_LOGI(tag, "Tunnel started");
  return true;
}

void poll_on_core(void *) {
  const auto station = desired_lease.load(std::memory_order_acquire);
  if (station != active_lease) {
    reset_interface();
    active_lease = station;
  }
  if (station && !connected_interface && esp_timer_get_time() >= next_attempt) {
    connected_interface = start_interface();
    if (!connected_interface) {
      reset_interface();
      next_attempt = esp_timer_get_time() + 5'000'000;
    }
  }
  if (connected_interface) {
    const bool connected = esp_wireguard_peer_is_up(&context) == ESP_OK;
    WireguardStatus next{.running = true, .connected = connected, .handshake_age = {}};
    time_t handshake{};
    if (esp_wireguard_latest_handshake(&context, &handshake) == ESP_OK) {
      const auto age = std::max<time_t>(0, std::time(nullptr) - handshake);
      next.handshake_age = std::min<time_t>(age, std::numeric_limits<uint32_t>::max());
    }
    publish(next);
    log_photo_route(connected);
  }
  callback_pending.store(false, std::memory_order_release);
}

}  // namespace

bool configure_wireguard(const WireguardSettings &saved, const std::string &immich_url) {
  if (!saved.enabled)
    return false;
  std::string error;
  if (!espframe_build_wireguard_config(saved.endpoint, saved.port, saved.address, saved.netmask, saved.allowed_ips,
                                       saved.peer_public_key, saved.private_key, settings, error)) {
    ESP_LOGW(tag, "Enabled, but %s is missing or invalid; tunnel stays off", error.c_str());
    return false;
  }
  set_wireguard_photo_url(immich_url);
  config.private_key = settings.private_key.c_str();
  config.public_key = settings.peer_public_key.c_str();
  config.address = settings.address.c_str();
  config.netmask = settings.netmask.c_str();
  config.port = settings.port;
  config.persistent_keepalive = 25;
  publish({.running = true, .connected = false, .handshake_age = {}});
  return true;
}

void set_wireguard_photo_url(const std::string &immich_url) {
  auto host = espframe_wireguard_url_host(immich_url);
  const std::lock_guard lock(status_mutex);
  photo_host = std::move(host);
}

void poll_wireguard(uint32_t station_lease) {
  desired_lease.store(station_lease, std::memory_order_release);
  if (!callback_pending.exchange(true, std::memory_order_acq_rel) &&
      tcpip_try_callback(poll_on_core, nullptr) != ERR_OK)
    callback_pending.store(false, std::memory_order_release);
}

WireguardStatus wireguard_status() {
  const std::lock_guard lock(status_mutex);
  return status;
}

}  // namespace espframe::native
