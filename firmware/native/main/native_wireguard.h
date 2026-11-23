#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace espframe::native {

struct WireguardSettings;

struct WireguardStatus {
  bool running{};
  bool connected{};
  std::optional<uint32_t> handshake_age;
};

bool configure_wireguard(const WireguardSettings &settings, const std::string &immich_url);
void set_wireguard_photo_url(const std::string &immich_url);
void poll_wireguard(uint32_t station_lease);
WireguardStatus wireguard_status();

}  // namespace espframe::native
