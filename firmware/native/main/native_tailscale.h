#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace espframe::native {

#if NATIVE_TAILSCALE
struct TailscaleStatus {
  const char *state{"off"};
  std::string address;
  std::string login_url;
};

enum class TailscaleAction : uint8_t { Connect, Disconnect };

struct TailscaleRoute {
  bool required{};
  uint32_t generation{};
  std::string address;
};

bool request_tailscale(TailscaleAction action);
void start_tailscale(std::string_view hostname, std::string_view immich_url, bool wireguard_active,
                     uint32_t settings_reset_epoch);
void update_tailscale_network(uint32_t station_lease);
uint32_t tailscale_generation();
bool use_tailscale_for(std::string_view url);
TailscaleRoute tailscale_route_for(std::string_view url);
TailscaleStatus tailscale_status();
#endif

}  // namespace espframe::native
