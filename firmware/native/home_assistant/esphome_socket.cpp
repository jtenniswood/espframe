#include "esphome_socket.h"

#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#else
#include <sys/select.h>
#include <sys/socket.h>
#endif

#include <cerrno>
#include <chrono>
#include <fcntl.h>

namespace espframe::native {
namespace {

using Time = std::chrono::steady_clock;
using namespace std::chrono_literals;

struct Connection {
  int fd;
  void *context;
  bool (*current)(void *);
  const Time::time_point handshake_deadline = Time::now() + 4s;
  Time::time_point idle_deadline{};
  bool authenticated{};

  bool connected() const { return !current || current(context); }

  static bool read(void *context, std::span<uint8_t> bytes) {
    auto &connection = *static_cast<Connection *>(context);
    // Home Assistant pings idle sessions. Allow time for that only after a
    // client has authenticated, so an incomplete handshake closes promptly.
    const auto deadline = connection.authenticated ? Time::now() + 75s : connection.handshake_deadline;
    while (!bytes.empty() && connection.connected() && Time::now() < deadline) {
      const auto count = recv(connection.fd, bytes.data(), bytes.size(), 0);
      if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        continue;
      if (count <= 0)
        return false;
      bytes = bytes.subspan(count);
    }
    if (bytes.empty() && connection.authenticated)
      connection.idle_deadline = Time::now() + 75s;
    return bytes.empty() && connection.connected();
  }

  static bool write(void *context, std::span<const uint8_t> bytes) {
    auto &connection = *static_cast<Connection *>(context);
    const auto deadline = Time::now() + 2s;
    while (!bytes.empty() && connection.connected() && Time::now() < deadline) {
      const auto count = send(connection.fd, bytes.data(), bytes.size(), 0);
      if (count < 0 && errno == EINTR)
        continue;
      if (count <= 0)
        return false;
      bytes = bytes.subspan(count);
    }
    return bytes.empty();
  }

  static void on_authenticated(void *context) {
    auto &connection = *static_cast<Connection *>(context);
    connection.authenticated = true;
    connection.idle_deadline = Time::now() + 75s;
  }

  static EspHomeReadiness poll(void *context) {
    auto &connection = *static_cast<Connection *>(context);
    if (!connection.connected() || Time::now() >= connection.idle_deadline)
      return EspHomeReadiness::Closed;
    fd_set readers;
    FD_ZERO(&readers);
    FD_SET(connection.fd, &readers);
    timeval timeout{0, 150'000};
    const int result = select(connection.fd + 1, &readers, nullptr, nullptr, &timeout);
    if (result < 0 && errno != EINTR)
      return EspHomeReadiness::Closed;
    return result > 0 ? EspHomeReadiness::Ready : EspHomeReadiness::Idle;
  }
};

}  // namespace

void serve_esphome_socket(int fd, const EspHomeIdentity &identity, std::span<const uint8_t, 32> key,
                          const EspHomeControls &controls, void *context, bool (*current)(void *)) {
  const timeval timeout{1, 0};
  if (fd < 0 || fd >= FD_SETSIZE || fcntl(fd, F_SETFL, 0) != 0 ||
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
      setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0)
    return;
  Connection connection{fd, context, current};
  serve_esphome({&connection, Connection::read, Connection::write, Connection::on_authenticated, Connection::poll},
                identity, key, controls);
}

}  // namespace espframe::native
