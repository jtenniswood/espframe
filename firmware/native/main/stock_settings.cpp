#include "stock_settings.h"
#include "stock_connection_preferences.h"
#include "stock_frame_identity.h"

#include "nvs.h"
#include "nvs_flash.h"

namespace espframe::native {
namespace {

template <size_t Size>
bool read_preference(nvs_handle_t storage, const char *key, std::array<uint8_t, Size> &value) {
  size_t size = value.size();
  return nvs_get_blob(storage, key, value.data(), &size) == ESP_OK && size == value.size();
}

}  // namespace

StockSettings read_stock_settings(StockSettingsRequest request) {
  // Non-erasing factory installs retain the ESPHome partition. The native
  // partition table and the NVS handle both mark it read-only.
  constexpr char partition[] = "stock_prefs";
  StockSettings result;
  if (!request.any() || nvs_flash_init_partition(partition) != ESP_OK)
    return result;

  nvs_handle_t storage;
  if (nvs_open_from_partition(partition, "esphome", NVS_READONLY, &storage) == ESP_OK) {
    if (request.wifi) {
      std::array<uint8_t, esphome_station_bytes> record{};
      if (read_preference(storage, esphome_station_key, record))
        result.wifi = read_stock_station(record);
    }
    if (request.immich) {
      std::array<uint8_t, esphome_url_bytes> url{};
      std::array<uint8_t, esphome_api_key_bytes> key{};
      if (read_preference(storage, esphome_immich_url_key, url) &&
          read_preference(storage, esphome_immich_api_key, key)) {
        result.immich_url = read_stock_preference_text(url);
        result.immich_key = read_stock_preference_text(key);
      }
    }
    if (request.pairing) {
      HomeAssistantKey key{};
      if (read_preference(storage, "88491486", key) && valid_home_assistant_key(key))
        result.pairing = key;
    }
    nvs_close(storage);
  }
  if (result.pairing)
    result.frame_name = read_stock_frame_name(partition);
  nvs_flash_deinit_partition(partition);
  return result;
}

}  // namespace espframe::native
