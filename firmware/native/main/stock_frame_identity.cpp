#include "stock_frame_identity.h"

#include "frame_identity_model.h"
#include "nvs.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace espframe::native {

std::optional<std::string> read_stock_frame_name(const char *partition) {
  // Stock saves a little-endian uint32_t version, a NUL-terminated
  // char[121], and three bytes of alignment padding on the ESP32-P4.
  constexpr std::array<uint8_t, 4> version{1, 0, 0, 0};
  constexpr size_t name_bytes = esphome::espframe::FRAME_NAME_MAX_BYTES + 1;
  std::array<uint8_t, version.size() + name_bytes + 3> record{};
  nvs_handle_t storage;
  if (nvs_open_from_partition(partition, "espframe_id", NVS_READONLY, &storage) != ESP_OK)
    return std::nullopt;
  size_t size = record.size();
  const auto result = nvs_get_blob(storage, "identity", record.data(), &size);
  nvs_close(storage);
  if (result != ESP_OK || size != record.size() || !std::equal(version.begin(), version.end(), record.begin()))
    return std::nullopt;

  const std::string_view name(reinterpret_cast<const char *>(record.data() + version.size()), name_bytes);
  const auto end = name.find('\0');
  std::string normalized;
  if (end == name.npos || !esphome::espframe::normalize_frame_name(std::string(name.substr(0, end)), normalized))
    return std::nullopt;
  return normalized;
}

}  // namespace espframe::native
