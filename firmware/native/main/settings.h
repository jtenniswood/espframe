#pragma once

#include "clock_settings.h"
#include "photo_framing.h"
#include "photo_source.h"
#include "screen_schedule.h"

#include <cstdint>
#include <string>

namespace espframe::native {

struct Settings {
  std::string ssid;
  std::string password;
  std::string immich_url;
  std::string immich_key;
  std::string web_password;
  uint32_t slide_seconds{15};
  uint8_t brightness_day{64};
  bool portrait_pairing{true};
  DisplayMode display_mode{DisplayMode::Fit};
  PhotoSource photo_source;
  ClockSettings clock;
  ScreenScheduleSettings schedule;

  static constexpr size_t max_json_size = 1536;
  bool read();
  static bool save(const char *json);
  static bool save_playback(const char *json);
};

}  // namespace espframe::native
