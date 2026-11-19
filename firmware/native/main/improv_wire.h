#pragma once

#include "wifi_credentials.h"

#include <cstdint>
#include <initializer_list>
#include <span>

namespace espframe::native::improv {

inline constexpr std::array<uint8_t, 7> header{'I', 'M', 'P', 'R', 'O', 'V', 1};
inline constexpr size_t payload_offset = 9;
enum class Type : uint8_t { State = 1, Error = 2, Command = 3, Result = 4 };
enum class Command : uint8_t { Wifi = 1, State = 2, Info = 3, Scan = 4, NetworkState = 7 };
enum class Read { Partial, Complete, Invalid };

struct Packet {
  std::array<uint8_t, payload_offset + 255 + 2> bytes{};
  size_t size{};
};

inline uint8_t checksum(std::span<const uint8_t> bytes) {
  uint8_t sum = 0;
  for (const auto byte : bytes)
    sum += byte;
  return sum;
}

class Reader {
 public:
  bool active() const { return length_ != 0; }
  void reset() { length_ = 0; }

  Read feed(uint8_t byte) {
    if (length_ < header.size() && byte != header[length_]) {
      reset();
      return Read::Invalid;
    }
    data_[length_++] = byte;
    if (length_ < payload_offset || length_ < payload_offset + data_[8] + 1)
      return Read::Partial;
    const auto body_length = length_ - 1;
    const bool valid = data_[7] == static_cast<uint8_t>(Type::Command) &&
                       checksum({data_.data(), body_length}) == data_[body_length];
    reset();
    return valid ? Read::Complete : Read::Invalid;
  }

  std::span<const uint8_t> payload() const { return {data_.data() + payload_offset, data_[8]}; }

 private:
  std::array<uint8_t, payload_offset + 255 + 1> data_{};
  size_t length_{};
};

inline Packet encode(Type type, std::span<const uint8_t> payload) {
  Packet packet;
  if (payload.size() > 255)
    return packet;
  std::copy(header.begin(), header.end(), packet.bytes.begin());
  packet.bytes[7] = static_cast<uint8_t>(type);
  packet.bytes[8] = payload.size();
  std::copy(payload.begin(), payload.end(), packet.bytes.begin() + payload_offset);
  const size_t end = payload_offset + payload.size();
  packet.bytes[end] = checksum({packet.bytes.data(), end});
  packet.bytes[end + 1] = '\n';
  packet.size = end + 2;
  return packet;
}

inline Packet result(Command command, std::initializer_list<std::string_view> fields) {
  std::array<uint8_t, 255> payload{};
  payload[0] = static_cast<uint8_t>(command);
  size_t length = 2;
  for (const auto field : fields) {
    if (field.size() >= payload.size() - length)
      return {};
    payload[length++] = field.size();
    std::copy(field.begin(), field.end(), payload.begin() + length);
    length += field.size();
  }
  payload[1] = length - 2;
  return encode(Type::Result, {payload.data(), length});
}

inline bool valid_command(std::span<const uint8_t> payload) {
  return payload.size() >= 2 && payload[1] == payload.size() - 2;
}

inline std::optional<WifiCredentials> read_wifi(std::span<const uint8_t> data) {
  return WifiCredentials::from_length_prefixed(data);
}

}  // namespace espframe::native::improv
