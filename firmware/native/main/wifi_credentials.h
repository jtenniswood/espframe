#pragma once

#include <algorithm>
#include <array>
#include <optional>
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
};

}  // namespace espframe::native
