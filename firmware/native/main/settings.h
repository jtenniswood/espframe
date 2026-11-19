#pragma once

#include <cstdint>
#include <string>

namespace espframe::native {

struct Settings {
  std::string ssid;
  std::string password;
  std::string immich_url;
  std::string immich_key;
  std::string web_password;
  uint16_t slide_seconds{15};
  bool portrait_pairing{true};

  static constexpr size_t max_json_size = 1536;
  bool read();
  static bool save(const char *json);
  static bool save_playback(const char *json);
};

}  // namespace espframe::native
