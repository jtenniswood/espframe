#include "native_tailscale.h"

#include "memory_trace.h"
#include "tailscale_url.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "microlink.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <array>
#include <atomic>
#include <mutex>
#include <utility>

namespace espframe::native {
namespace {

constexpr char store_namespace[] = "frame_tailscale";
enum class Enrollment : uint8_t { Off, On, Forget };
std::atomic<unsigned> pending{};
std::atomic<uint32_t> station{};
std::atomic<uint32_t> generation{};
std::atomic<bool> wireguard{};
std::atomic<TaskHandle_t> worker{};
std::string device_name;
uint32_t reset_epoch{};
std::mutex client_mutex;
uint32_t next_generation{};
std::mutex status_mutex;
TailscaleStatus status;
microlink_t *client{};
Enrollment enrollment{};
bool reconnect_after_forget{};
bool restart_client{};
uint32_t active_lease{};

void publish(TailscaleStatus next) {
  const std::lock_guard lock(status_mutex);
  status = std::move(next);
}

bool save_enrollment(Enrollment value) {
  nvs_handle_t storage{};
  if (nvs_open(store_namespace, NVS_READWRITE, &storage) != ESP_OK)
    return false;
  const uint64_t record = (uint64_t{reset_epoch} << 8) | static_cast<uint8_t>(value);
  const bool saved = nvs_set_u64(storage, "enrollment", record) == ESP_OK && nvs_commit(storage) == ESP_OK;
  nvs_close(storage);
  if (saved)
    enrollment = value;
  return saved;
}

Enrollment restore_enrollment() {
  nvs_handle_t storage{};
  if (nvs_flash_init() != ESP_OK || nvs_open(store_namespace, NVS_READONLY, &storage) != ESP_OK)
    return Enrollment::Off;
  uint64_t record{};
  const bool saved = nvs_get_u64(storage, "enrollment", &record) == ESP_OK;
  nvs_close(storage);
  if (!saved)
    return Enrollment::Off;
  // A reset may have happened under an image built without Tailscale.
  if ((record >> 8) != reset_epoch)
    return Enrollment::Forget;
  const auto state = static_cast<uint8_t>(record);
  return state <= static_cast<uint8_t>(Enrollment::Forget) ? static_cast<Enrollment>(state) : Enrollment::Forget;
}

bool stop_client() {
  if (!client)
    return true;
  if (microlink_stop(client) != ESP_OK)
    return false;
  microlink_destroy(client);
  client = nullptr;
  active_lease = 0;
  trace_memory("tailscale-stopped");
  return true;
}

TailscaleStatus read_status() {
  switch (microlink_get_state(client)) {
    case ML_STATE_NEEDS_LOGIN: {
      std::array<char, MICROLINK_AUTH_URL_MAX> url{};
      if (microlink_get_auth_url(client, url.data(), url.size()) == ESP_OK &&
          std::string_view(url.data()).starts_with("https://login.tailscale.com/"))
        return {.state = "needs_login", .address = {}, .login_url = url.data()};
      return {.state = "connecting", .address = {}, .login_url = {}};
    }
    case ML_STATE_CONNECTED: {
      std::array<char, 16> address{};
      microlink_ip_to_str(microlink_get_vpn_ip(client), address.data());
      return {.state = "connected", .address = address.data(), .login_url = {}};
    }
    case ML_STATE_AUTH_FAILED:
      return {.state = "auth_failed", .address = {}, .login_url = {}};
    case ML_STATE_ERROR:
      return {.state = "error", .address = {}, .login_url = {}};
    default:
      return {.state = "connecting", .address = {}, .login_url = {}};
  }
}

void service_tailscale(uint32_t station_lease) {
  const auto action = pending.exchange(0);
  uint32_t previous_generation = generation.load();
  if (action || enrollment == Enrollment::Forget || restart_client || station_lease != active_lease)
    generation.store(0, std::memory_order_release);
  if (action) {
    const bool connect = action == static_cast<unsigned>(TailscaleAction::Connect) + 1;
    const auto status = tailscale_status();
    const bool forget = !connect || enrollment == Enrollment::Forget || std::string_view(status.state) == "auth_failed";
    if (!save_enrollment(forget ? Enrollment::Forget : Enrollment::On))
      return publish({.state = "error", .address = {}, .login_url = {}});
    reconnect_after_forget = connect;
    restart_client = true;
  }
  if (restart_client || enrollment == Enrollment::Forget) {
    if (!stop_client())
      return publish({.state = "error", .address = {}, .login_url = {}});
    restart_client = false;
    previous_generation = 0;
  }
  if (enrollment == Enrollment::Forget) {
    if (microlink_factory_reset() != ESP_OK || !save_enrollment(reconnect_after_forget ? Enrollment::On : Enrollment::Off))
      return publish({.state = "error", .address = {}, .login_url = {}});
  }
  if (enrollment != Enrollment::On)
    return publish({});
  if (!station_lease)
    return publish({.state = "waiting_wifi", .address = {}, .login_url = {}});
  if (station_lease != active_lease || action)
    previous_generation = 0;
  if (!client) {
    trace_memory("tailscale-before-start");
    microlink_config_t config{};
    config.device_name = device_name.empty() ? nullptr : device_name.c_str();
    client = microlink_init(&config);
    if (!client || microlink_start(client) != ESP_OK) {
      stop_client();
      save_enrollment(Enrollment::Off);
      return publish({.state = "error", .address = {}, .login_url = {}});
    }
    active_lease = station_lease;
    trace_memory("tailscale-started");
  } else if (station_lease != active_lease && microlink_rebind(client) == ESP_OK) {
    active_lease = station_lease;
  }

  auto next = read_status();
  const auto previous = tailscale_status();
  if (std::string_view(next.state) != previous.state) {
    const std::string stage = std::string("tailscale-") + next.state;
    trace_memory(stage.c_str());
  }
  if (std::string_view(next.state) == "connected" && station_lease == active_lease && station_lease == station.load()) {
    if (!previous_generation || previous.address != next.address) {
      if (++next_generation == 0)
        ++next_generation;
      previous_generation = next_generation;
    }
    generation.store(previous_generation, std::memory_order_release);
  } else {
    generation.store(0, std::memory_order_release);
  }
  publish(std::move(next));
}

void service(void *) {
  enrollment = restore_enrollment();
  while (true) {
    {
      const std::lock_guard lock(client_mutex);
      service_tailscale(station.load(std::memory_order_acquire));
    }
    ulTaskNotifyTake(pdTRUE, enrollment == Enrollment::Off ? portMAX_DELAY : pdMS_TO_TICKS(500));
  }
}

}  // namespace

void start_tailscale(std::string_view hostname, bool wireguard_active, uint32_t settings_reset_epoch) {
  wireguard.store(wireguard_active, std::memory_order_release);
  if (wireguard_active)
    return;
  device_name = hostname;
  reset_epoch = settings_reset_epoch;
  TaskHandle_t task{};
  if (xTaskCreate(service, "tailscale", 4096, nullptr, 2, &task) == pdPASS)
    worker.store(task, std::memory_order_release);
  else
    publish({.state = "error", .address = {}, .login_url = {}});
}

void update_tailscale_network(uint32_t station_lease) {
  if (station.exchange(station_lease, std::memory_order_acq_rel) != station_lease) {
    if (const auto task = worker.load(std::memory_order_acquire))
      xTaskNotifyGive(task);
  }
}

bool request_tailscale(TailscaleAction action) {
  const auto task = worker.load(std::memory_order_acquire);
  unsigned idle = 0;
  if (!task || !pending.compare_exchange_strong(idle, static_cast<unsigned>(action) + 1))
    return false;
  xTaskNotifyGive(task);
  return true;
}

uint32_t tailscale_generation() {
  return station.load(std::memory_order_acquire) ? generation.load(std::memory_order_acquire) : 0;
}

bool use_tailscale_for(std::string_view url) {
  return !wireguard.load(std::memory_order_acquire) && !tailnet_host(url).hostname.empty();
}

TailscaleRoute tailscale_route_for(std::string_view url) {
  if (!use_tailscale_for(url))
    return {};
  TailscaleRoute route{.required = true, .generation = 0, .address = {}};
  const auto before = tailscale_generation();
  if (!before)
    return route;
  const std::lock_guard lock(client_mutex);
  if (before != tailscale_generation() || !client || !microlink_is_connected(client))
    return route;
  const auto host = tailnet_host(url);
  std::string hostname(host.hostname);
  if (hostname.ends_with('.'))
    hostname.pop_back();
  const auto peer = host.ipv4 != 0 ? host.ipv4 : microlink_resolve(client, hostname.c_str());
  if ((peer & 0xffc00000U) != 0x64400000U)
    return route;
  std::array<char, 16> address{};
  microlink_ip_to_str(peer, address.data());
  route.address = address.data();
  route.generation = before;
  return route;
}

TailscaleStatus tailscale_status() {
  const std::lock_guard lock(status_mutex);
  return status;
}

}  // namespace espframe::native
