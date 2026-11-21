#include "improv_serial.h"

#include "driver/uart.h"
#include "esp_netif_ip_addr.h"

#include <cstdio>
#include <string>

namespace espframe::native {
namespace {

using improv::Command;
using improv::Type;

enum class State : uint8_t { Ready = 2, Provisioning = 3, Provisioned = 4 };
enum class Error : uint8_t { None, Invalid, Unsupported, UnableToConnect, Internal = 0xff };

void send(const improv::Packet &packet) {
  if (packet.size)
    uart_write_bytes(UART_NUM_0, packet.bytes.data(), packet.size);
}

void single(Type type, uint8_t value) { send(improv::encode(type, {&value, 1})); }
void error(Error value) { single(Type::Error, static_cast<uint8_t>(value)); }

std::string settings_url(NativeNetwork::Address address) {
  if (!address.ipv4 || address.web != NativeNetwork::WebStatus::Ready)
    return {};
  const esp_ip4_addr_t ip{address.ipv4};
  char url[32]{};
  std::snprintf(url, sizeof(url), "http://" IPSTR "/", IP2STR(&ip));
  return url;
}

}  // namespace

bool ImprovSerial::consume(uint8_t byte, bool line_start, int64_t now_us) {
  if (!reader_.active() && (!line_start || byte != improv::header.front()))
    return false;
  last_byte_us_ = now_us;
  const auto status = reader_.feed(byte);
  if (status == improv::Read::Complete)
    command_(reader_.payload());
  else if (status == improv::Read::Invalid)
    error(Error::Invalid);
  return true;
}

void ImprovSerial::state_() {
  const auto state = pending_ == NativeNetwork::Request::Provision ? State::Provisioning
                     : network_.address().ipv4 ? State::Provisioned : State::Ready;
  single(Type::State, static_cast<uint8_t>(state));
}

void ImprovSerial::send_url_(Command command) {
  const auto url = settings_url(network_.address());
  send(url.empty() ? improv::result(command, {}) : improv::result(command, {url}));
}

void ImprovSerial::command_(std::span<const uint8_t> payload) {
  if (!improv::valid_command(payload)) {
    error(Error::Invalid);
    return;
  }
  const auto command = static_cast<Command>(payload[0]);
  if (command != Command::Wifi && payload[1] != 0) {
    error(Error::Invalid);
    return;
  }
  error(Error::None);
  switch (command) {
    case Command::Wifi: {
      const auto wifi = improv::read_wifi(payload.subspan(2));
      if (!wifi) {
        error(Error::Invalid);
      } else if (busy() || !network_.submit(NativeNetwork::Request::Provision, *wifi)) {
        error(Error::Internal);
      } else {
        pending_ = NativeNetwork::Request::Provision;
        state_();
      }
      return;
    }
    case Command::State:
      state_();
      if (pending_ != NativeNetwork::Request::Provision && network_.address().ipv4)
        send_url_(Command::State);
      return;
    case Command::Info: {
      const auto firmware = std::string("Native Espframe V") + NATIVE_BOARD[1];
      send(improv::result(command, {firmware, "prototype", "ESP32-P4", "Espframe"}));
      return;
    }
    case Command::Scan:
      if (!busy() && network_.submit(NativeNetwork::Request::Scan))
        pending_ = NativeNetwork::Request::Scan;
      else
        send(improv::result(command, {}));
      return;
    case Command::NetworkState: {
      const auto address = network_.address();
      const auto url = settings_url(address);
      const auto flags = address.ipv4 ? "3" : "2";
      send(url.empty() ? improv::result(command, {flags}) : improv::result(command, {flags, url}));
      return;
    }
  }
  error(Error::Unsupported);
}

void ImprovSerial::poll(int64_t now_us) {
  if (reader_.active() && now_us - last_byte_us_ >= 1'000'000)
    reader_.reset();
  if (!pending_)
    return;
  NativeNetwork::Reply reply{};
  if (!network_.poll(reply))
    return;
  pending_.reset();
  if (reply.request == NativeNetwork::Request::Provision) {
    state_();
    if (reply.success)
      send_url_(Command::Wifi);
    else
      error(Error::UnableToConnect);
  } else {
    for (size_t i = 0; i < reply.count; ++i) {
      const auto &ap = reply.networks[i];
      char rssi[6]{};
      std::snprintf(rssi, sizeof(rssi), "%d", ap.rssi);
      send(improv::result(Command::Scan, {ap.ssid.data(), rssi, ap.secured ? "YES" : "NO"}));
    }
    send(improv::result(Command::Scan, {}));
  }
}

}  // namespace espframe::native
