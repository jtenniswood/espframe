#include "stock_home_assistant_key.h"

#include "nvs.h"
#include "nvs_flash.h"

namespace espframe::native {

std::optional<HomeAssistantKey> read_stock_home_assistant_key() {
  // This partition is marked readonly in the native table. ESPHome
  // stores its runtime Noise PSK as a raw 32-byte preference blob.
  constexpr char partition[] = "stock_prefs";
  constexpr char preference[] = "88491486";
  if (nvs_flash_init_partition(partition) != ESP_OK)
    return {};

  HomeAssistantKey key{};
  nvs_handle_t storage;
  bool read = false;
  if (nvs_open_from_partition(partition, "esphome", NVS_READONLY, &storage) == ESP_OK) {
    size_t size = key.size();
    read = nvs_get_blob(storage, preference, key.data(), &size) == ESP_OK && size == key.size();
    nvs_close(storage);
  }
  nvs_flash_deinit_partition(partition);
  if (!read || !valid_home_assistant_key(key))
    return {};
  return key;
}

}  // namespace espframe::native
