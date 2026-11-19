#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace espframe::native {

class ScreenControl;

struct EspHomeIdentity {
  std::string_view name;
  std::string_view friendly_name;
  std::string_view model;
  std::array<uint8_t, 6> mac;
};

struct EspHomeStream {
  void *context;
  // Transfer the entire span or fail. The caller owns socket deadlines.
  bool (*read)(void *, std::span<uint8_t>);
  bool (*write)(void *, std::span<const uint8_t>);
};

// Serve one authenticated client on its network task. This function returns
// when the client disconnects or a transfer fails. It never owns the socket.
void serve_esphome(EspHomeStream stream, const EspHomeIdentity &identity,
                   std::span<const uint8_t, 32> key, ScreenControl &screen);

}  // namespace espframe::native
