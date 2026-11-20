#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace espframe::native {

class ScreenControl;
enum class PhotoPreset : uint8_t;

inline constexpr char esphome_noise_protocol[] = "Noise_NNpsk0_25519_ChaChaPoly_SHA256";

struct EspHomeIdentity {
  std::string_view name;
  std::string_view friendly_name;
  std::string_view model;
  std::array<uint8_t, 6> mac;
};

struct EspHomeMemorySample {
  std::optional<float> heap_free_percent;
  std::optional<float> heap_largest_block;
  std::optional<float> psram_free_percent;
  std::optional<float> psram_largest_block;
};

struct EspHomeControls {
  ScreenControl &screen;
  const void *context;
  uint32_t (*interval)(const void *);
  bool (*set_interval)(const void *, uint32_t);
  PhotoPreset (*photo_preset)(const void *);
  bool (*set_photo_preset)(const void *, PhotoPreset);
  std::string_view station_address;
  std::optional<int8_t> (*wifi_rssi)(const void *);
  std::string_view reset_reason;
  void (*restart)(const void *);
  bool (*set_connection_timeout)(const void *, uint32_t);
  bool (*set_device_language)(const void *, uint8_t);
  EspHomeMemorySample (*memory)(const void *);
};

enum class EspHomeReadiness { Ready, Idle, Closed };

struct EspHomeStream {
  void *context;
  // Transfer the entire span or fail. The caller owns socket deadlines.
  bool (*read)(void *, std::span<uint8_t>);
  bool (*write)(void *, std::span<const uint8_t>);
  void (*authenticated)(void *){};
  EspHomeReadiness (*poll)(void *){};
};

std::array<char, 17> esphome_mac_text(const std::array<uint8_t, 6> &mac, bool pretty = false);

// Serve one authenticated client on its network task. This function returns
// when the client disconnects or a transfer fails. It never owns the socket.
void serve_esphome(EspHomeStream stream, const EspHomeIdentity &identity,
                   std::span<const uint8_t, 32> key, const EspHomeControls &controls);

}  // namespace espframe::native
