#include "esphome_session.h"

#include "esp_random.h"
#include "../../../main/photo_source.h"
#include "../../../main/screen_control.h"

extern "C" void noise_rand_bytes(void *output, size_t size) {
  // This link test has no socket or ADC users. The ESP32-P4 bootloader
  // leaves its SAR ADC entropy source enabled at application startup.
  esp_fill_random(output, size);
}

extern "C" void app_main() {
  using namespace espframe::native;
  const EspHomeStream closed{
      nullptr,
      [](void *, std::span<uint8_t>) { return false; },
      [](void *, std::span<const uint8_t>) { return false; },
  };
  // Reference the normal session entry point to link its entire call graph.
  const std::array<uint8_t, 32> no_key{};
  const EspHomeControls controls{
      screen_control, nullptr,
      [](const void *) -> uint32_t { return 15; },
      [](const void *, uint32_t) { return false; },
      [](const void *) { return PhotoPreset::All; },
      [](const void *, PhotoPreset) { return false; },
      "127.0.0.1",
      [](const void *) -> std::optional<int8_t> { return {}; },
      "power-on event",
      [](const void *) {},
      [](const void *, uint32_t) { return false; },
      [](const void *, uint8_t) { return false; },
      [](const void *) { return EspHomeMemorySample{}; },
  };
  serve_esphome(closed, {"espframe-link-test", {}, {}, {2, 0, 0, 0, 0, 1}}, no_key, controls);
}
