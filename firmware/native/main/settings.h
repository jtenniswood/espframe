#pragma once

#include <string>

namespace espframe::native {

struct Settings {
  std::string ssid;
  std::string password;
  std::string immich_url;
  std::string immich_key;

  static constexpr size_t max_json_size = 1536;
  bool read();
  static bool save(const char *json);
};

}  // namespace espframe::native
