#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace espframe::native {

using HostedVersion = std::array<uint32_t, 3>;

struct C6Release {
  HostedVersion version;
  std::string url;
  std::array<uint8_t, 32> sha256;
};

inline constexpr std::string_view hosted_firmware_origin = "https://esphome.github.io/esp-hosted-firmware/";
inline constexpr size_t c6_manifest_limit = 16 * 1024;

std::optional<HostedVersion> parse_hosted_version(std::string_view version);
std::string hosted_version_text(const HostedVersion &version);
std::optional<C6Release> read_c6_release(std::string_view json, const HostedVersion &host);

}  // namespace espframe::native
