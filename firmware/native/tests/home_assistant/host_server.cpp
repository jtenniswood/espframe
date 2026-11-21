#include "esphome_socket.h"
#include "home_assistant_key.h"
#include "i18n.h"
#include "photo_metadata.h"
#include "photo_source.h"
#include "screen_control.h"
#include "slideshow_interval.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace espframe::native;

namespace {

struct TestDevice {
  std::atomic<int8_t> rssi{-65};
  std::atomic<uint8_t> memory_sample{};
  int connection{-1};
  mutable bool rebooted{};
};

}  // namespace

extern "C" void noise_rand_bytes(void *output, size_t size) {
  if (size > 256 || getentropy(output, size) != 0)
    std::abort();
}

int main() {
  const int listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) {
    std::perror("socket");
    return 1;
  }
  std::signal(SIGPIPE, SIG_IGN);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t length = sizeof(address);
  if (bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
      listen(listener, 4) != 0 || getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
    std::perror("start loopback server");
    close(listener);
    return 1;
  }
  std::cout << ntohs(address.sin_port) << std::endl;

  constexpr EspHomeIdentity identity{
      "espframe-ha-test", "Espframe Test", "Guition test board", {2, 0xaa, 0xbb, 0xcc, 0xdd, 0xee},
  };
  const auto fixture_key = decode_home_assistant_key("AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA=");
  if (!fixture_key)
    return 1;
  active_slideshow_interval.store(17);
  screen_control.publish_backlight(64);
  TestDevice device;
  EspHomeControls controls{
      screen_control, &device,
      [](const void *) { return active_slideshow_interval.load(); },
      [](const void *, uint32_t seconds) {
        if (seconds == 60)
          return false;  // Simulate a failed persistence operation.
        active_slideshow_interval.store(seconds);
        return true;
      },
      [](const void *) { return active_photo_preset.load(); },
      [](const void *, PhotoPreset preset) {
        if (preset == PhotoPreset::Album)
          return false;  // Simulate a failed persistence operation.
        active_photo_preset.store(preset);
        return true;
      },
      "127.0.0.1",
      [](const void *context) -> std::optional<int8_t> {
        const auto value = static_cast<const TestDevice *>(context)->rssi.load();
        return value == 0 ? std::nullopt : std::optional<int8_t>{value};
      },
      {},
      [](const void *context) {
        const auto &device = *static_cast<const TestDevice *>(context);
        device.rebooted = true;
        shutdown(device.connection, SHUT_RDWR);
      },
      [](const void *, uint32_t seconds) {
        if (seconds == 120)
          return false;  // Simulate a failed persistence operation.
        active_connection_timeout.store(seconds);
        return true;
      },
      [](const void *, uint8_t language) {
        if (language == find_espframe_language("de"))
          return false;  // Simulate a failed persistence operation.
        active_device_language.store(language);
        return true;
      },
      [](const void *context) -> EspHomeMemorySample {
        switch (static_cast<const TestDevice *>(context)->memory_sample.load()) {
          case 1: return {35.25f, 8192.0f, std::nullopt, 0.0f};
          case 2: return {30.0f, 8192.0f, 41.5f, 262144.0f};
          default: return {35.25f, 32768.0f, 54.5f, 8'388'608.0f};
        }
      },
  };
  std::jthread player([&device](std::stop_token stop) {
    ScreenSchedule schedule{{true, 6, 22, 60}};
    std::optional<uint8_t> manual_brightness;
    int hour = 8;
    while (!stop.stop_requested()) {
      pollfd input{STDIN_FILENO, POLLIN, 0};
      char hour_change{};
      if (poll(&input, 1, 0) > 0 && (input.revents & POLLIN) != 0 && read(STDIN_FILENO, &hour_change, 1) == 1) {
        switch (hour_change) {
          case 'n': hour = 23; break;
          case 'x': device.rssi.store(0); break;
          case 'w': device.rssi.store(-110); break;
          case 's': device.rssi.store(-45); break;
          case 'p': active_device_language.store(find_espframe_language("he")); break;
          case 't': active_connection_timeout.store(900); break;
          case 'i': active_slideshow_interval.store(23); break;
          case 'f': active_photo_preset.store(PhotoPreset::Memories); break;
          case 'm': device.memory_sample.store(1); break;
          case 'r': device.memory_sample.store(2); break;
        }
      }
      const bool commanded = screen_control.apply_pending(schedule, 1'000, hour, manual_brightness);
      const bool awake = schedule.update(hour, 1'000, false);
      if (!awake)
        manual_brightness.reset();
      screen_control.publish_backlight(awake ? manual_brightness.value_or(64) : 0);
      if (commanded)
        screen_control.completed();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  while (true) {
    int connection = accept(listener, nullptr, nullptr);
    if (connection < 0)
      break;
    device.connection = connection;
    controls.reset_reason = device.rebooted ? "software via esp_restart" : "power-on event";
    serve_esphome_socket(connection, identity, *fixture_key, controls);
    close(connection);
  }
  close(listener);
}
