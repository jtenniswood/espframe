#pragma once

#include "home_assistant_key.h"
#include "wifi_credentials.h"

#include <string>

namespace espframe::native {

struct StockSettingsRequest {
  bool wifi{};
  bool immich{};
  bool pairing{};

  bool any() const { return wifi || immich || pairing; }
};

struct StockSettings {
  std::optional<WifiCredentials> wifi;
  std::string immich_url;
  std::string immich_key;
  std::optional<HomeAssistantKey> pairing;
  std::optional<std::string> frame_name;
};

StockSettings read_stock_settings(StockSettingsRequest request);

}  // namespace espframe::native
