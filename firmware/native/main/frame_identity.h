#pragma once

#include "frame_identity_model.h"

#include <array>
#include <cstdint>
#include <string>

namespace espframe::native {

inline constexpr char frame_identity_path[] = "/espframe/api/v1/identity";

using FrameMac = std::array<uint8_t, 6>;

struct FrameIdentity {
  std::string name;
  std::string friendly_name;
  std::string hostname;
  std::string mac_suffix;
};

inline bool valid_frame_mac(const FrameMac &mac) {
  return (mac.front() & 1) == 0 && std::any_of(mac.begin(), mac.end(), [](uint8_t byte) { return byte != 0; });
}

inline FrameIdentity make_frame_identity(const std::string &name, const FrameMac &mac, const char *default_hostname,
                                         const char *default_friendly_name, bool use_stock_default_hostname = false) {
  FrameIdentity identity{name, name.empty() ? default_friendly_name : name, default_hostname, {}};
  if (!valid_frame_mac(mac))
    return identity;
  constexpr char hex[] = "0123456789abcdef";
  for (const uint8_t byte : {mac[4], mac[5]}) {
    identity.mac_suffix += hex[byte >> 4];
    identity.mac_suffix += hex[byte & 15];
  }
  if (!name.empty()) {
    identity.hostname = esphome::espframe::frame_hostname(name, identity.mac_suffix);
  } else if (use_stock_default_hostname) {
    // ESPHome's name_add_mac_suffix uses the last three MAC bytes.
    identity.hostname += '-';
    identity.hostname += hex[mac[3] >> 4];
    identity.hostname += hex[mac[3] & 15];
    identity.hostname += identity.mac_suffix;
  }
  return identity;
}

}  // namespace espframe::native
