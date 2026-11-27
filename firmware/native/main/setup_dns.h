#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace espframe::native {

// Turn a single-question DNS query into a captive-portal reply in place.
inline size_t setup_dns_reply(std::span<uint8_t> packet, size_t received, std::array<uint8_t, 4> address) {
  constexpr size_t header_size = 12;
  constexpr size_t question_fields = 4;
  if (received < header_size + 1 + question_fields || received > packet.size() ||
      (packet[2] & 0xf8) || packet[4] != 0 || packet[5] != 1)
    return 0;

  size_t end = header_size;
  while (end < received && packet[end] != 0) {
    const auto label = packet[end++];
    if (label > 63 || label > received - end)
      return 0;
    end += label;
  }
  if (end >= received || received - end <= question_fields || end - header_size >= 255)
    return 0;
  ++end;
  const bool ipv4 = packet[end] == 0 && packet[end + 1] == 1 && packet[end + 2] == 0 && packet[end + 3] == 1;
  end += question_fields;

  // No answer for other question types. Any EDNS options from the input are omitted.
  packet[2] = 0x84;
  packet[3] = 0;
  std::fill(packet.begin() + 6, packet.begin() + header_size, 0);
  if (!ipv4)
    return end;
  constexpr std::array<uint8_t, 12> answer{0xc0, header_size, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4};
  if (packet.size() - end < answer.size() + address.size())
    return 0;
  packet[7] = 1;
  std::copy(answer.begin(), answer.end(), packet.begin() + end);
  std::copy(address.begin(), address.end(), packet.begin() + end + answer.size());
  return end + answer.size() + address.size();
}

}  // namespace espframe::native
