#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace espframe::native {

using PreviewVersion = std::array<uint32_t, 4>;
using FirmwareDigest = std::array<uint8_t, 32>;

struct NativeRelease {
  std::string version;
  uint32_t size{};
  FirmwareDigest sha256{};
  FirmwareDigest elf_sha256{};
};

inline constexpr size_t max_release_index_bytes = 2048;

std::optional<PreviewVersion> parse_preview_version(std::string_view version);
std::optional<std::vector<NativeRelease>> read_release_index(std::string_view json, std::string_view board);

}  // namespace espframe::native
