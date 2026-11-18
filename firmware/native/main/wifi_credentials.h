#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace espframe::native {

struct WifiCredentials {
  std::array<char, 33> ssid{};
  std::array<char, 65> password{};

  bool configured() const { return ssid[0] != '\0'; }

  static std::optional<WifiCredentials> from(std::string_view name, std::string_view secret) {
    if (name.empty() || name.size() > 32 || secret.size() > 64 ||
        name.find('\0') != name.npos || secret.find('\0') != secret.npos)
      return {};
    WifiCredentials result;
    std::copy(name.begin(), name.end(), result.ssid.begin());
    std::copy(secret.begin(), secret.end(), result.password.begin());
    return result;
  }

  static std::optional<WifiCredentials> from_length_prefixed(std::span<const uint8_t> data) {
    if (data.size() < 2 || data[0] >= data.size() - 1)
      return {};
    const auto ssid_length = data[0];
    const auto password_length = data[1 + ssid_length];
    if (data.size() != 2u + ssid_length + password_length)
      return {};
    const auto *text = reinterpret_cast<const char *>(data.data());
    return from({text + 1, ssid_length}, {text + 2 + ssid_length, password_length});
  }
};

}  // namespace espframe::native
