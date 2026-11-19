#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace espframe::native {

using HomeAssistantKey = std::array<uint8_t, 32>;

inline std::optional<HomeAssistantKey> decode_home_assistant_key(std::string_view encoded) {
  constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (encoded.size() != 44 || encoded.back() != '=')
    return {};
  HomeAssistantKey key{};
  size_t output = 0;
  unsigned bits = 0;
  unsigned pending = 0;
  for (const char character : encoded.substr(0, 43)) {
    const auto value = alphabet.find(character);
    if (value == alphabet.npos)
      return {};
    bits = (bits << 6) | static_cast<unsigned>(value);
    pending += 6;
    if (pending >= 8) {
      pending -= 8;
      key[output++] = static_cast<uint8_t>(bits >> pending);
      bits &= (1u << pending) - 1;
    }
  }
  if (bits != 0 || std::all_of(key.begin(), key.end(), [](uint8_t value) { return value == 0; }))
    return {};
  return key;
}

}  // namespace espframe::native
