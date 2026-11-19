#include "esphome_session.h"
#include "screen_control.h"
#include "slideshow_interval.h"

#include <noise/protocol.h>

#include <algorithm>
#include <memory>
#include <optional>

namespace espframe::native {
namespace {

// ESPHome 2026.11 native API Noise framing, using Noise-C for all encryption.
constexpr uint8_t noise_marker = 1;
constexpr size_t frame_header_size = 3;
constexpr size_t message_header_size = 4;
constexpr size_t noise_tag_size = 16;
constexpr size_t max_handshake_size = 64;
constexpr size_t max_payload_size = 1024;
constexpr std::string_view prologue_prefix = "NoiseAPIInit";

enum class Message : uint16_t {
  Hello = 1, HelloResponse = 2, Disconnect = 5, DisconnectResponse = 6,
  Ping = 7, PingResponse = 8, DeviceInfo = 9, DeviceInfoResponse = 10,
  ListEntities = 11, EntitiesDone = 19, SubscribeStates = 20,
  Select = 52, SelectState = 53, SelectCommand = 54,
  Button = 61, ButtonCommand = 62, DeviceCapabilities = 149, DeviceCapabilitiesResponse = 150,
};

uint16_t read_be16(std::span<const uint8_t> data) {
  return static_cast<uint16_t>(data[0] << 8 | data[1]);
}

void write_be16(std::span<uint8_t> data, uint16_t value) {
  data[0] = value >> 8;
  data[1] = value;
}

uint32_t read_le32(std::span<const uint8_t> data) {
  return data[0] | static_cast<uint32_t>(data[1]) << 8 |
         static_cast<uint32_t>(data[2]) << 16 | static_cast<uint32_t>(data[3]) << 24;
}

class NoiseSession {
 public:
  explicit NoiseSession(EspHomeStream stream) : stream_(stream) {}
  ~NoiseSession() {
    if (tx_)
      noise_cipherstate_free(tx_);
    if (rx_)
      noise_cipherstate_free(rx_);
  }
  NoiseSession(const NoiseSession &) = delete;
  NoiseSession &operator=(const NoiseSession &) = delete;

  bool handshake(std::span<const uint8_t, 32> key, std::string_view name, std::string_view mac) {
    const auto hello = read_frame_(max_handshake_size);
    if (!hello)
      return false;

    // The initiator's first unencrypted frame is bound to the handshake.
    std::array<uint8_t, prologue_prefix.size() + 2 + max_handshake_size> prologue{};
    std::copy(prologue_prefix.begin(), prologue_prefix.end(), prologue.begin());
    write_be16(std::span{prologue}.subspan(prologue_prefix.size()), hello->size());
    std::copy(hello->begin(), hello->end(), prologue.begin() + prologue_prefix.size() + 2);

    auto data = std::span(frame_).subspan(frame_header_size);
    data[0] = 1;  // Selected protocol.
    auto tail = std::copy(name.begin(), name.end(), data.begin() + 1);
    *tail++ = 0;
    tail = std::copy(mac.begin(), mac.end(), tail);
    *tail++ = 0;
    if (!write_frame_(tail - data.begin()))
      return false;

    NoiseHandshakeState *raw{};
    const auto created = noise_handshakestate_new_by_name(
        &raw, esphome_noise_protocol, NOISE_ROLE_RESPONDER);
    std::unique_ptr<NoiseHandshakeState, decltype(&noise_handshakestate_free)> state(raw, noise_handshakestate_free);
    if (created != NOISE_ERROR_NONE ||
        noise_handshakestate_set_pre_shared_key(raw, key.data(), key.size()) != NOISE_ERROR_NONE ||
        noise_handshakestate_set_prologue(raw, prologue.data(),
                                         prologue_prefix.size() + 2 + hello->size()) != NOISE_ERROR_NONE ||
        noise_handshakestate_start(raw) != NOISE_ERROR_NONE)
      return false;

    const auto request = read_frame_(max_handshake_size);
    if (!request || request->empty() || request->front() != 0)
      return false;
    auto body = request->subspan(1);
    NoiseBuffer message{body.data(), body.size(), body.size()};
    const auto result = noise_handshakestate_read_message(raw, &message, nullptr);
    if (result == NOISE_ERROR_MAC_FAILURE) {
      constexpr std::string_view reason = "Handshake MAC failure";
      data[0] = 1;
      std::copy(reason.begin(), reason.end(), data.begin() + 1);
      write_frame_(reason.size() + 1);
    }
    if (result != NOISE_ERROR_NONE)
      return false;

    data[0] = 0;
    message = {data.data() + 1, 0, max_handshake_size - 1};
    return noise_handshakestate_write_message(raw, &message, nullptr) == NOISE_ERROR_NONE &&
           noise_handshakestate_split(raw, &tx_, &rx_) == NOISE_ERROR_NONE &&
           write_frame_(message.size + 1);
  }

  bool receive(Message &type, std::span<const uint8_t> &payload) {
    const auto data = read_frame_(frame_.size() - frame_header_size);
    if (!data || data->size() < message_header_size + noise_tag_size)
      return false;
    NoiseBuffer message{data->data(), data->size(), data->size()};
    if (noise_cipherstate_decrypt(rx_, &message) != NOISE_ERROR_NONE)
      return false;
    const auto size = read_be16(data->subspan(2));
    if (message.size != message_header_size + size)
      return false;
    type = static_cast<Message>(read_be16(*data));
    payload = data->subspan(message_header_size, size);
    return true;
  }

  bool send(Message type, std::span<const uint8_t> payload = {}) {
    if (payload.size() > max_payload_size)
      return false;
    auto data = std::span(frame_).subspan(frame_header_size);
    write_be16(data, static_cast<uint16_t>(type));
    write_be16(data.subspan(2), payload.size());
    std::copy(payload.begin(), payload.end(), data.begin() + message_header_size);
    NoiseBuffer message{data.data(), message_header_size + payload.size(), data.size()};
    return noise_cipherstate_encrypt(tx_, &message) == NOISE_ERROR_NONE && write_frame_(message.size);
  }

 private:
  std::optional<std::span<uint8_t>> read_frame_(size_t limit) {
    auto bytes = std::span(frame_);
    if (!stream_.read(stream_.context, bytes.first(frame_header_size)) || bytes[0] != noise_marker)
      return std::nullopt;
    const auto length = read_be16(bytes.subspan(1));
    if (length > limit)
      return std::nullopt;
    auto data = bytes.subspan(frame_header_size, length);
    if (!data.empty() && !stream_.read(stream_.context, data))
      return std::nullopt;
    return data;
  }

  bool write_frame_(size_t size) {
    frame_[0] = noise_marker;
    write_be16(std::span{frame_}.subspan(1), size);
    return stream_.write(stream_.context, std::span{frame_}.first(frame_header_size + size));
  }

  EspHomeStream stream_;
  NoiseCipherState *tx_{};
  NoiseCipherState *rx_{};
  std::array<uint8_t, frame_header_size + message_header_size + max_payload_size + noise_tag_size> frame_{};
};

class ProtoReply {
 public:
  void number(uint8_t field, uint32_t value) {
    append_varint(field << 3);
    append_varint(value);
  }

  void text(uint8_t field, std::string_view value) {
    append_varint((field << 3) | 2);
    append_varint(value.size());
    for (const unsigned char byte : value)
      append(byte);
  }

  void fixed32(uint8_t field, uint32_t value) {
    append_varint((field << 3) | 5);
    for (unsigned index = 0; index < 4; ++index, value >>= 8)
      append(static_cast<uint8_t>(value));
  }

  bool send(NoiseSession &session, Message type) const {
    return !overflow_ && session.send(type, std::span{data_}.first(size_));
  }

 private:
  void append(uint8_t byte) {
    if (size_ == data_.size()) {
      overflow_ = true;
      return;
    }
    data_[size_++] = byte;
  }

  void append_varint(uint32_t value) {
    while (value >= 128) {
      append(static_cast<uint8_t>(value) | 0x80);
      value >>= 7;
    }
    append(value);
  }

  std::array<uint8_t, 384> data_{};
  size_t size_{};
  bool overflow_{};
};

struct ScreenButton {
  uint32_t key;
  const char *object_id;
  const char *name;
  const char *icon;
  ScreenAction action;
};

constexpr ScreenButton buttons[] = {
    {2655868716U, "screen_sleep", "Screen: Sleep", "mdi:power-sleep", ScreenAction::Sleep},
    {1917547667U, "screen_wake", "Screen: Wake", "mdi:power", ScreenAction::Wake},
};

// Generated by stock ESPHome from the display name. Keep the same entity.
constexpr uint32_t interval_key = 3029912432U;

}  // namespace

std::array<char, 17> esphome_mac_text(const std::array<uint8_t, 6> &mac, bool pretty) {
  const char *digits = pretty ? "0123456789ABCDEF" : "0123456789abcdef";
  std::array<char, 17> result{};
  size_t position = 0;
  for (const uint8_t byte : mac) {
    if (pretty && position != 0)
      result[position++] = ':';
    result[position++] = digits[byte >> 4];
    result[position++] = digits[byte & 15];
  }
  return result;
}

namespace {

bool send_hello(NoiseSession &session, std::string_view name) {
  ProtoReply reply;
  reply.number(1, 1);  // API version 1.19.
  reply.number(2, 19);
  reply.text(3, "Espframe native");
  reply.text(4, name);
  return reply.send(session, Message::HelloResponse);
}

bool send_device_info(NoiseSession &session, const EspHomeIdentity &identity) {
  ProtoReply reply;
  const auto address = esphome_mac_text(identity.mac, true);
  reply.text(2, identity.name);
  reply.text(3, {address.data(), address.size()});
  reply.text(6, identity.model);
  reply.text(8, "jtenniswood.immich-frame");
  reply.text(12, "Guition");
  reply.text(13, identity.friendly_name);
  reply.number(19, 1);  // API encryption supported.
  return reply.send(session, Message::DeviceInfoResponse);
}

bool stock_interval(uint32_t seconds) {
  return std::find(stock_slideshow_intervals.begin(), stock_slideshow_intervals.end(), seconds) !=
         stock_slideshow_intervals.end();
}

bool send_entities(NoiseSession &session, uint32_t custom_interval) {
  for (const auto &button : buttons) {
    ProtoReply reply;
    reply.text(1, button.object_id);
    reply.fixed32(2, button.key);
    reply.text(3, button.name);
    reply.text(5, button.icon);
    if (!reply.send(session, Message::Button))
      return false;
  }
  ProtoReply interval;
  interval.text(1, "photos_slideshow_interval");
  interval.fixed32(2, interval_key);
  interval.text(3, "Photos: Slideshow Interval");
  interval.text(5, "mdi:timer-outline");
  for (const auto seconds : stock_slideshow_intervals)
    interval.text(6, interval_label(seconds));
  if (custom_interval)
    interval.text(6, interval_label(custom_interval));
  return interval.send(session, Message::Select) && session.send(Message::EntitiesDone);
}

bool send_interval(NoiseSession &session, uint32_t seconds) {
  ProtoReply reply;
  reply.fixed32(1, interval_key);
  reply.text(2, interval_label(seconds));
  return reply.send(session, Message::SelectState);
}

uint32_t requested_interval(std::span<const uint8_t> payload, uint32_t custom_interval) {
  std::string_view option;
  bool has_key = false;
  while (!payload.empty()) {
    const auto tag = payload.front();
    payload = payload.subspan(1);
    if (tag == 0x0d && !has_key && payload.size() >= 4 && read_le32(payload) == interval_key) {
      has_key = true;
      payload = payload.subspan(4);
    } else if (tag == 0x12 && option.empty() && payload.size() >= 2 &&
               payload.front() > 0 && payload.front() <= 16 && payload.front() < payload.size()) {
      const auto length = payload.front();
      option = {reinterpret_cast<const char *>(payload.data() + 1), length};
      payload = payload.subspan(1 + length);
    } else if (tag == 0x18 && !payload.empty() && payload.front() == 0) {
      payload = payload.subspan(1);
    } else {
      return 0;
    }
  }
  if (!has_key || option.empty())
    return 0;
  const auto seconds = interval_seconds(option);
  return (stock_interval(seconds) || (custom_interval && seconds == custom_interval)) &&
         interval_label(seconds) == option ? seconds : 0;
}

void accept_button(std::span<const uint8_t> payload, ScreenControl &screen) {
  // aioesphomeapi sends a fixed32 key. The parent device's ID is zero,
  // omitted by proto3. Honor only commands for that device.
  if (payload.size() != 5 || payload.front() != 0x0d)
    return;
  const uint32_t key = read_le32(payload.subspan(1));
  for (const auto &button : buttons) {
    if (key == button.key) {
      screen.submit(button.action);
      return;
    }
  }
}

}  // namespace

void serve_esphome(EspHomeStream stream, const EspHomeIdentity &identity,
                   std::span<const uint8_t, 32> key, const EspHomeControls &controls) {
  if (identity.name.empty() || identity.name.size() > 31 || identity.name.find('\0') != std::string_view::npos ||
      identity.friendly_name.size() > 120 || identity.model.size() > 127 || (identity.mac.front() & 1) != 0 ||
      std::all_of(identity.mac.begin(), identity.mac.end(), [](uint8_t byte) { return byte == 0; }) ||
      std::all_of(key.begin(), key.end(), [](uint8_t byte) { return byte == 0; }))
    return;
  static const bool noise_ready = noise_init_framework() == NOISE_ERROR_NONE;
  if (!noise_ready)
    return;

  NoiseSession session(stream);
  const auto address = esphome_mac_text(identity.mac);
  Message type{};
  std::span<const uint8_t> body;
  if (!session.handshake(key, identity.name, {address.data(), 12}) ||
      !session.receive(type, body) || type != Message::Hello || !send_hello(session, identity.name))
    return;

  if (stream.authenticated)
    stream.authenticated(stream.context);

  const auto initial_interval = controls.interval(controls.context);
  const auto custom_interval = stock_interval(initial_interval) ? 0 : initial_interval;
  bool subscribed = false;
  while (session.receive(type, body)) {
    bool sent = true;
    switch (type) {
      case Message::Disconnect: session.send(Message::DisconnectResponse); return;
      case Message::Ping: sent = session.send(Message::PingResponse); break;
      case Message::DeviceInfo: sent = send_device_info(session, identity); break;
      case Message::DeviceCapabilities: sent = session.send(Message::DeviceCapabilitiesResponse); break;
      case Message::ListEntities: sent = send_entities(session, custom_interval); break;
      case Message::SubscribeStates:
        subscribed = true;
        sent = send_interval(session, controls.interval(controls.context));
        break;
      case Message::SelectCommand:
        if (const auto seconds = requested_interval(body, custom_interval)) {
          controls.set_interval(controls.context, seconds);
          if (subscribed)
            sent = send_interval(session, controls.interval(controls.context));
        }
        break;
      case Message::ButtonCommand: accept_button(body, controls.screen); break;
      default: break;
    }
    if (!sent)
      return;
  }
}

}  // namespace espframe::native
