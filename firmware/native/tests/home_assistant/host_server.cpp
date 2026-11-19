#include "esphome_session.h"
#include "screen_control.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
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

namespace {

bool read_exact(void *context, std::span<uint8_t> bytes) {
  const int fd = *static_cast<int *>(context);
  while (!bytes.empty()) {
    const auto count = recv(fd, bytes.data(), bytes.size(), 0);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return false;
    bytes = bytes.subspan(count);
  }
  return true;
}

bool write_exact(void *context, std::span<const uint8_t> bytes) {
  const int fd = *static_cast<int *>(context);
  while (!bytes.empty()) {
    const auto count = send(fd, bytes.data(), bytes.size(), 0);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return false;
    bytes = bytes.subspan(count);
  }
  return true;
}

}  // namespace

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
  std::array<uint8_t, 32> fixture_key{};
  for (size_t index = 0; index < fixture_key.size(); ++index)
    fixture_key[index] = index + 1;
  std::jthread player([](std::stop_token stop) {
    ScreenSchedule schedule{{true, 6, 22, 60}};
    while (!stop.stop_requested()) {
      if (screen_control.apply_pending(schedule, 1'000)) {
        const bool awake = schedule.update(8, 1'000, false);
        screen_control.completed();
        std::cout << (awake ? "awake" : "asleep") << std::endl;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  while (true) {
    int connection = accept(listener, nullptr, nullptr);
    if (connection < 0)
      break;
    const timeval timeout{3, 0};
    setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    serve_esphome({&connection, read_exact, write_exact}, identity, fixture_key, screen_control);
    close(connection);
  }
  close(listener);
}
