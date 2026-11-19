#include "esphome_socket.h"
#include "home_assistant_key.h"
#include "screen_control.h"
#include "slideshow_interval.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace espframe::native;

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
  const EspHomeControls controls{
      screen_control, nullptr,
      [](const void *) { return active_slideshow_interval.load(); },
      [](const void *, uint32_t seconds) {
        if (seconds == 60)
          return false;  // Simulate a failed persistence operation.
        active_slideshow_interval.store(seconds);
        return true;
      },
  };
  std::jthread player([](std::stop_token stop) {
    ScreenSchedule schedule{{true, 6, 22, 60}};
    std::optional<uint8_t> manual_brightness;
    int hour = 8;
    while (!stop.stop_requested()) {
      pollfd input{STDIN_FILENO, POLLIN, 0};
      char hour_change{};
      if (poll(&input, 1, 0) > 0 && (input.revents & POLLIN) != 0 && read(STDIN_FILENO, &hour_change, 1) == 1)
        hour = hour_change == 'n' ? 23 : 8;
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
    serve_esphome_socket(connection, identity, *fixture_key, controls);
    close(connection);
  }
  close(listener);
}
