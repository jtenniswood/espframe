#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace espframe::native {

using HomeAssistantKey = std::array<uint8_t, 32>;

inline constexpr std::string_view home_assistant_key_alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline bool valid_home_assistant_key(const HomeAssistantKey &key) {
  return std::any_of(key.begin(), key.end(), [](uint8_t value) { return value != 0; });
}

inline std::optional<HomeAssistantKey> decode_home_assistant_key(std::string_view encoded) {
  if (encoded.size() != 44 || encoded.back() != '=')
    return {};
  HomeAssistantKey key{};
  size_t output = 0;
  unsigned bits = 0;
  unsigned pending = 0;
  for (const char character : encoded.substr(0, 43)) {
    const auto value = home_assistant_key_alphabet.find(character);
    if (value == home_assistant_key_alphabet.npos)
      return {};
    bits = (bits << 6) | static_cast<unsigned>(value);
    pending += 6;
    if (pending >= 8) {
      pending -= 8;
      key[output++] = static_cast<uint8_t>(bits >> pending);
      bits &= (1u << pending) - 1;
    }
  }
  if (bits != 0 || !valid_home_assistant_key(key))
    return {};
  return key;
}

inline std::string encode_home_assistant_key(const HomeAssistantKey &key) {
  std::string encoded;
  encoded.reserve(44);
  unsigned bits = 0;
  unsigned pending = 0;
  for (const auto value : key) {
    bits = (bits << 8) | value;
    pending += 8;
    while (pending >= 6) {
      pending -= 6;
      encoded += home_assistant_key_alphabet[(bits >> pending) & 63];
    }
  }
  encoded += home_assistant_key_alphabet[(bits << (6 - pending)) & 63];
  encoded += '=';
  return encoded;
}

}  // namespace espframe::native
