#include "esphome_session.h"
#include "i18n.h"
#include "photo_metadata.h"
#include "photo_source.h"
#include "screen_control.h"
#include "slideshow_interval.h"

#include <noise/protocol.h>

#include <algorithm>
#include <bit>
#include <cmath>
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
  ListEntities = 11, BinarySensor = 12, Light = 15, Sensor = 16, TextSensor = 18,
  EntitiesDone = 19, SubscribeStates = 20, BinarySensorState = 21, LightState = 24,
  SensorState = 25, TextSensorState = 27, LightCommand = 32,
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
constexpr uint32_t photo_source_key = 3905368779U;
constexpr uint32_t backlight_key = 2211332998U;
constexpr uint32_t online_key = 3727615758U;
constexpr uint32_t wifi_strength_key = 3943756064U;
constexpr uint32_t ip_address_key = 3394986237U;
constexpr uint32_t reboot_key = 2194406813U;
constexpr uint32_t reset_reason_key = 2962208445U;
constexpr uint32_t device_language_key = 4083618279U;
constexpr uint32_t connection_timeout_key = 1991622891U;
constexpr uint8_t brightness_color_mode = 3;
constexpr uint8_t configuration_category = 1;
constexpr uint8_t diagnostic_category = 2;

struct MemorySensor {
  uint32_t key;
  const char *object_id;
  const char *name;
  const char *unit;
  uint8_t decimals;
  std::optional<float> EspHomeMemorySample::*reading;
};

constexpr MemorySensor memory_sensors[] = {
    {3519121973U, "memory__heap_free", "Memory: Heap Free", "%", 1, &EspHomeMemorySample::heap_free_percent},
    {3632003571U, "memory__heap_largest_block", "Memory: Heap Largest Block", "B", 0,
     &EspHomeMemorySample::heap_largest_block},
    {276662424U, "memory__psram_free", "Memory: PSRAM Free", "%", 1, &EspHomeMemorySample::psram_free_percent},
    {1399957840U, "memory__psram_largest_block", "Memory: PSRAM Largest Block", "B", 0,
     &EspHomeMemorySample::psram_largest_block},
};

constexpr auto stock_language_options = [] {
  std::array<std::string_view, espframe_i18n_catalogue::LANGUAGE_COUNT> options{};
  std::ranges::copy(espframe_i18n_catalogue::LANGUAGES, options.begin());
  // ESPHome lists English first, then sorts the remaining language codes.
  std::sort(options.begin() + 1, options.end());
  return options;
}();

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

bool send_device_entities(NoiseSession &session) {
  {
    ProtoReply reboot;
    reboot.text(1, "device__reboot_screen");
    reboot.fixed32(2, reboot_key);
    reboot.text(3, "Device: Reboot Screen");
    reboot.text(5, "mdi:restart");
    reboot.number(7, configuration_category);
    reboot.text(8, "restart");
    if (!reboot.send(session, Message::Button))
      return false;
  }
  ProtoReply reason;
  reason.text(1, "reset_reason");
  reason.fixed32(2, reset_reason_key);
  reason.text(3, "Reset Reason");
  reason.text(5, "mdi:restart");
  reason.number(7, diagnostic_category);
  return reason.send(session, Message::TextSensor);
}

bool send_network_entities(NoiseSession &session) {
  {
    ProtoReply reply;
    reply.text(1, "network__online");
    reply.fixed32(2, online_key);
    reply.text(3, "Network: Online");
    reply.text(5, "connectivity");
    reply.number(6, 1);
    reply.text(8, "mdi:check-network");
    reply.number(9, diagnostic_category);
    if (!reply.send(session, Message::BinarySensor))
      return false;
  }
  {
    ProtoReply reply;
    reply.text(1, "network__wifi_strength");
    reply.fixed32(2, wifi_strength_key);
    reply.text(3, "Network: WiFi Strength");
    reply.text(5, "mdi:wifi");
    reply.text(6, "%");
    reply.text(9, "signal_strength");
    reply.number(10, 1);  // Measurement.
    reply.number(13, diagnostic_category);
    if (!reply.send(session, Message::Sensor))
      return false;
  }
  ProtoReply reply;
  reply.text(1, "network__ip_address");
  reply.fixed32(2, ip_address_key);
  reply.text(3, "Network: IP Address");
  reply.text(5, "mdi:ip-network");
  reply.number(7, diagnostic_category);
  return reply.send(session, Message::TextSensor);
}

bool send_display_entities(NoiseSession &session) {
  {
    ProtoReply language;
    language.text(1, "device__language");
    language.fixed32(2, device_language_key);
    language.text(3, "Device: Language");
    language.text(5, "mdi:translate");
    for (const auto option : stock_language_options)
      language.text(6, option);
    language.number(8, configuration_category);
    if (!language.send(session, Message::Select))
      return false;
  }
  ProtoReply timeout;
  timeout.text(1, "screen__connection_timeout");
  timeout.fixed32(2, connection_timeout_key);
  timeout.text(3, "Screen: Connection Timeout");
  timeout.text(5, "mdi:connection");
  for (const auto seconds : connection_timeouts)
    timeout.text(6, interval_label(seconds));
  return timeout.send(session, Message::Select);
}

bool send_memory_entities(NoiseSession &session) {
  for (const auto &sensor : memory_sensors) {
    ProtoReply reply;
    reply.text(1, sensor.object_id);
    reply.fixed32(2, sensor.key);
    reply.text(3, sensor.name);
    reply.text(5, "mdi:memory");
    reply.text(6, sensor.unit);
    reply.number(7, sensor.decimals);
    reply.number(13, diagnostic_category);
    if (!reply.send(session, Message::Sensor))
      return false;
  }
  return true;
}

bool send_entities(NoiseSession &session, uint32_t custom_interval) {
  ProtoReply backlight;
  backlight.text(1, "screen_backlight");
  backlight.fixed32(2, backlight_key);
  backlight.text(3, "Screen: Backlight");
  backlight.number(12, brightness_color_mode);
  if (!backlight.send(session, Message::Light))
    return false;

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
  if (!interval.send(session, Message::Select))
    return false;
  ProtoReply source;
  source.text(1, "photos__source");
  source.fixed32(2, photo_source_key);
  source.text(3, "Photos: Source");
  source.text(5, "mdi:image-filter-frames");
  for (const auto option : photo_presets)
    source.text(6, option);
  return source.send(session, Message::Select) && send_network_entities(session) && send_device_entities(session) &&
         send_display_entities(session) && send_memory_entities(session) && session.send(Message::EntitiesDone);
}

bool send_select_state(NoiseSession &session, uint32_t key, std::string_view option) {
  ProtoReply reply;
  reply.fixed32(1, key);
  reply.text(2, option);
  return reply.send(session, Message::SelectState);
}

struct SelectStates {
  uint32_t interval{};
  PhotoPreset source{};
  uint8_t language{};
  uint32_t timeout{};

  bool report_interval(NoiseSession &session, const EspHomeControls &controls, bool force = false) {
    const auto current = controls.interval(controls.context);
    if (!force && interval == current)
      return true;
    interval = current;
    return send_select_state(session, interval_key, interval_label(interval));
  }

  bool report_source(NoiseSession &session, const EspHomeControls &controls, bool force = false) {
    const auto current = controls.photo_preset(controls.context);
    if (!force && source == current)
      return true;
    source = current;
    return send_select_state(session, photo_source_key, photo_preset_name(source));
  }

  bool report_language(NoiseSession &session, bool force = false) {
    const auto current = active_device_language.load(std::memory_order_relaxed);
    if (!force && language == current)
      return true;
    language = current;
    return send_select_state(session, device_language_key, espframe_i18n_catalogue::LANGUAGES[language]);
  }

  bool report_timeout(NoiseSession &session, bool force = false) {
    const auto current = active_connection_timeout.load(std::memory_order_relaxed);
    if (!force && timeout == current)
      return true;
    timeout = current;
    return send_select_state(session, connection_timeout_key, interval_label(timeout));
  }
};

bool send_backlight(NoiseSession &session, uint8_t level) {
  ProtoReply reply;
  reply.fixed32(1, backlight_key);
  reply.number(2, level != 0);
  reply.fixed32(3, std::bit_cast<uint32_t>(level / 100.0f));
  reply.number(11, brightness_color_mode);
  return reply.send(session, Message::LightState);
}

std::optional<uint8_t> wifi_strength(const EspHomeControls &controls) {
  if (const auto rssi = controls.wifi_rssi(controls.context))
    return std::clamp(2 * (*rssi + 100), 0, 100);
  return {};
}

bool send_sensor_state(NoiseSession &session, uint32_t key, std::optional<float> value) {
  ProtoReply reply;
  reply.fixed32(1, key);
  if (value)
    reply.fixed32(2, std::bit_cast<uint32_t>(*value));
  else
    reply.number(3, 1);
  return reply.send(session, Message::SensorState);
}

bool report_memory(NoiseSession &session, const EspHomeControls &controls, EspHomeMemorySample &reported,
                   bool force = false) {
  const auto sample = controls.memory(controls.context);
  for (const auto &sensor : memory_sensors) {
    const auto &value = sample.*sensor.reading;
    const auto &previous = reported.*sensor.reading;
    if ((force || value != previous) && !send_sensor_state(session, sensor.key, value))
      return false;
  }
  reported = sample;
  return true;
}

bool send_text_state(NoiseSession &session, uint32_t key, std::string_view value) {
  ProtoReply reply;
  reply.fixed32(1, key);
  if (value.empty())
    reply.number(3, 1);
  else
    reply.text(2, value);
  return reply.send(session, Message::TextSensorState);
}

bool send_network_status(NoiseSession &session, std::string_view address) {
  ProtoReply reply;
  reply.fixed32(1, online_key);
  reply.number(2, 1);  // The authenticated station connection is online.
  return reply.send(session, Message::BinarySensorState) && send_text_state(session, ip_address_key, address);
}

std::optional<uint32_t> take_varint(std::span<const uint8_t> &payload) {
  uint32_t value{};
  for (unsigned shift = 0; shift <= 28 && !payload.empty(); shift += 7) {
    const auto byte = payload.front();
    payload = payload.subspan(1);
    if (shift == 28 && byte > 0x0f)
      break;
    value |= static_cast<uint32_t>(byte & 0x7f) << shift;
    if (byte < 0x80)
      return value;
  }
  return std::nullopt;
}

void accept_backlight(std::span<const uint8_t> payload, ScreenControl &screen) {
  uint32_t key{}, device{};
  bool has_state{}, on{}, has_brightness{};
  float level{};
  while (!payload.empty()) {
    const auto tag = take_varint(payload);
    if (!tag || *tag < 8)
      return;
    const auto wire_type = *tag & 7;
    uint32_t value{};
    if (wire_type == 0) {
      const auto parsed = take_varint(payload);
      if (!parsed)
        return;
      value = *parsed;
    } else if (wire_type == 5) {
      if (payload.size() < 4)
        return;
      value = read_le32(payload);
      payload = payload.subspan(4);
    } else if (wire_type == 2) {
      const auto length = take_varint(payload);
      if (!length || *length > payload.size())
        return;
      payload = payload.subspan(*length);
    } else {
      return;
    }
    switch (*tag) {
      case 0x0d: key = value; break;
      case 0x10: has_state = value != 0; break;
      case 0x18: on = value != 0; break;
      case 0x20: has_brightness = value != 0; break;
      case 0x2d: level = std::bit_cast<float>(value); break;
      case 28 << 3: device = value; break;
      default: break;  // Other light options do not change the monochromatic level.
    }
  }
  if (key != backlight_key || device != 0 || (has_brightness && !(level >= 0.0f && level <= 1.0f)))
    return;
  if (has_state && !on) {
    screen.submit(ScreenAction::Sleep);
  } else if (has_brightness) {
    const auto percent = level == 0.0f ? 0L : std::max(1L, std::lround(level * 100.0f));
    screen.submit_backlight(static_cast<uint8_t>(percent));
  } else if (has_state) {
    screen.submit_backlight(std::nullopt);
  }
}

struct SelectRequest {
  uint32_t key{};
  std::string_view option;
};

std::optional<SelectRequest> select_request(std::span<const uint8_t> payload) {
  SelectRequest request;
  while (!payload.empty()) {
    const auto tag = payload.front();
    payload = payload.subspan(1);
    if (tag == 0x0d && request.key == 0 && payload.size() >= 4 && read_le32(payload) != 0) {
      request.key = read_le32(payload);
      payload = payload.subspan(4);
    } else if (tag == 0x12 && request.option.empty() && payload.size() >= 2 &&
               payload.front() > 0 && payload.front() <= 16 && payload.front() < payload.size()) {
      const auto length = payload.front();
      request.option = {reinterpret_cast<const char *>(payload.data() + 1), length};
      payload = payload.subspan(1 + length);
    } else if (tag == 0x18 && !payload.empty() && payload.front() == 0) {
      payload = payload.subspan(1);
    } else {
      return {};
    }
  }
  if (request.key == 0 || request.option.empty())
    return {};
  return request;
}

uint32_t requested_interval(std::string_view option, uint32_t custom_interval) {
  const auto seconds = interval_seconds(option);
  return (stock_interval(seconds) || (custom_interval && seconds == custom_interval)) &&
         interval_label(seconds) == option ? seconds : 0;
}

void accept_button(std::span<const uint8_t> payload, const EspHomeControls &controls) {
  // aioesphomeapi sends a fixed32 key. The parent device's ID is zero,
  // omitted by proto3. Honor only commands for that device.
  if (payload.size() != 5 || payload.front() != 0x0d)
    return;
  const uint32_t key = read_le32(payload.subspan(1));
  if (key == reboot_key) {
    controls.restart(controls.context);
    return;
  }
  for (const auto &button : buttons) {
    if (key == button.key) {
      controls.screen.submit(button.action);
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
  auto custom_interval = stock_interval(initial_interval) ? 0 : initial_interval;
  bool subscribed = false;
  uint8_t reported_backlight{};
  std::optional<uint8_t> reported_strength;
  EspHomeMemorySample reported_memory;
  SelectStates selects;
  while (true) {
    if (subscribed && stream.poll) {
      const auto level = controls.screen.backlight();
      if (level != reported_backlight) {
        if (!send_backlight(session, level))
          return;
        reported_backlight = level;
      }
      const auto strength = wifi_strength(controls);
      if (strength != reported_strength) {
        if (!send_sensor_state(session, wifi_strength_key, strength))
          return;
        reported_strength = strength;
      }
      if (!selects.report_interval(session, controls) || !selects.report_source(session, controls) ||
          !selects.report_language(session) || !selects.report_timeout(session) ||
          !report_memory(session, controls, reported_memory))
        return;
      const auto ready = stream.poll(stream.context);
      if (ready == EspHomeReadiness::Idle)
        continue;
      if (ready == EspHomeReadiness::Closed)
        return;
    }
    if (!session.receive(type, body))
      return;
    bool sent = true;
    switch (type) {
      case Message::Disconnect: session.send(Message::DisconnectResponse); return;
      case Message::Ping: sent = session.send(Message::PingResponse); break;
      case Message::DeviceInfo: sent = send_device_info(session, identity); break;
      case Message::DeviceCapabilities: sent = session.send(Message::DeviceCapabilitiesResponse); break;
      case Message::ListEntities: {
        const auto interval = controls.interval(controls.context);
        custom_interval = stock_interval(interval) ? 0 : interval;
        sent = send_entities(session, custom_interval);
        break;
      }
      case Message::SubscribeStates:
        subscribed = true;
        reported_backlight = controls.screen.backlight();
        reported_strength = wifi_strength(controls);
        sent = selects.report_interval(session, controls, true) &&
               send_backlight(session, reported_backlight) &&
               selects.report_source(session, controls, true) &&
               send_network_status(session, controls.station_address) &&
               send_sensor_state(session, wifi_strength_key, reported_strength) &&
               send_text_state(session, reset_reason_key, controls.reset_reason) &&
               selects.report_language(session, true) && selects.report_timeout(session, true) &&
               report_memory(session, controls, reported_memory, true);
        break;
      case Message::SelectCommand:
        if (const auto request = select_request(body)) {
          if (request->key == interval_key) {
            if (const auto seconds = requested_interval(request->option, custom_interval)) {
              controls.set_interval(controls.context, seconds);
              if (subscribed)
                sent = selects.report_interval(session, controls, true);
            }
          } else if (request->key == photo_source_key) {
            if (const auto preset = parse_photo_preset(request->option)) {
              controls.set_photo_preset(controls.context, *preset);
              if (subscribed)
                sent = selects.report_source(session, controls, true);
            }
          } else if (request->key == device_language_key) {
            const auto language = find_espframe_language(request->option);
            if (language < espframe_i18n_catalogue::LANGUAGE_COUNT) {
              controls.set_device_language(controls.context, static_cast<uint8_t>(language));
              if (subscribed)
                sent = selects.report_language(session, true);
            }
          } else if (request->key == connection_timeout_key) {
            const auto timeout = interval_seconds(request->option);
            if (valid_connection_timeout(timeout) && interval_label(timeout) == request->option) {
              controls.set_connection_timeout(controls.context, timeout);
              if (subscribed)
                sent = selects.report_timeout(session, true);
            }
          }
        }
        break;
      case Message::ButtonCommand: accept_button(body, controls); break;
      case Message::LightCommand: accept_backlight(body, controls.screen); break;
      default: break;
    }
    if (!sent)
      return;
  }
}

}  // namespace espframe::native
