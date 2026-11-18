#pragma once

#include "capture_date.h"

#include "clock_settings.h"
#include "photo_framing.h"
#include "photo_metadata.h"
#include "photo_source.h"
#include "screen_schedule.h"
#include "screen_tone.h"
#include "slide_geometry.h"
#include "wifi_credentials.h"

#include <cstdint>
#include <optional>
#include <string>

namespace espframe::native {

inline constexpr const char *screen_rotation_name(ScreenRotation rotation) {
  return rotation == ScreenRotation::UpsideDown ? "180" : "0";
}

struct Settings {
  std::string ssid;
  std::string password;
  std::string immich_url;
  std::string immich_key;
  std::string web_password;
  uint32_t slide_seconds{15};
  uint8_t brightness_day{64};
  std::optional<uint8_t> brightness_night;
  bool portrait_pairing{true};
  DisplayMode display_mode{DisplayMode::Fit};
  ScreenRotation screen_rotation{ScreenRotation::Normal};
  PhotoSource photo_source;
  CaptureDateSettings capture_dates;
  PhotoMetadataSettings metadata;
  ClockSettings clock;
  ScreenScheduleSettings schedule;
  ScreenToneSettings tone;

  uint8_t night_brightness() const { return brightness_night.value_or(brightness_day); }
  bool has_immich() const { return !immich_url.empty() && !immich_key.empty(); }

  static constexpr size_t max_json_size = 3072;
  bool read();
  static bool save(const char *json);
  static bool save_wifi(const WifiCredentials &wifi);
  static bool save_playback(const char *json);
};

}  // namespace espframe::native
