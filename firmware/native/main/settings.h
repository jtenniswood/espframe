#pragma once

#include "capture_date.h"

#include "clock_settings.h"
#include "photo_framing.h"
#include "photo_filters.h"
#include "photo_metadata.h"
#include "photo_orientation.h"
#include "photo_source.h"
#include "screen_schedule.h"
#include "screen_tone.h"
#include "slide_geometry.h"
#include "stock_settings.h"
#include "wifi_credentials.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct cJSON;

namespace espframe::native {

inline constexpr const char *screen_rotation_name(ScreenRotation rotation) {
  return rotation == ScreenRotation::UpsideDown ? "180" : "0";
}

enum class ResetMode { Customization, Factory };

struct PhotoFeedSettings {
  bool portrait_pairing{true};
  bool portrait_pairs_only{};
  uint8_t portrait_pairing_days{};
  DisplayMode display_mode{DisplayMode::Fit};
  PairFraming pair_framing{PairFraming::FollowPhotos};
  PhotoOrientation photo_orientation{PhotoOrientation::Any};
  PhotoSource photo_source;
  PhotoFilters photo_filters;
  CaptureDateSettings capture_dates;
  PhotoMetadataSettings metadata;

  bool pairs_portraits() const { return portrait_pairing && photo_orientation != PhotoOrientation::Landscape; }
  bool requires_portrait_pair() const { return pairs_portraits() && portrait_pairs_only; }
  DisplayMode pair_display_mode() const {
    switch (pair_framing) {
      case PairFraming::Fit: return DisplayMode::Fit;
      case PairFraming::Fill: return DisplayMode::Fill;
      case PairFraming::FollowPhotos: return display_mode;
    }
    return display_mode;
  }
};

struct Settings : PhotoFeedSettings {
  enum class UpdateResult { Saved, SavedLive, Unchanged, Invalid, Stale, Failed };
  enum class UsbSaveResult { Saved, Rejected, WifiUnavailable, ImmichUnavailable, PairingUnavailable };

  std::string ssid;
  std::string password;
  std::string immich_url;
  std::string immich_key;
  std::string prepared_slide_url;
  std::string prepared_slide_token;
  std::string web_password;
  std::string home_assistant_key;
  std::string frame_name;
  bool home_assistant_key_set{};
  uint32_t reset_epoch{};
  uint32_t slide_seconds{15};
  std::string transition{"Cross Fade"};
  std::string transition_speed{"Fast"};
  uint8_t brightness_day{64};
  std::optional<uint8_t> brightness_night;
  ScreenRotation screen_rotation{ScreenRotation::Normal};
  ClockSettings clock;
  ScreenScheduleSettings schedule;
  ScreenToneSettings tone;

  uint8_t night_brightness() const { return brightness_night.value_or(brightness_day); }
  bool has_immich() const { return !immich_url.empty() && !immich_key.empty(); }
  bool has_prepared_slides() const { return !prepared_slide_url.empty() && !prepared_slide_token.empty(); }
  static constexpr size_t max_json_size = 6144;
  bool read();
  bool read_esphome_trial();
  static bool save(const char *json, std::optional<uint32_t> expected_epoch = {});
  static UsbSaveResult save_usb(const char *json, std::optional<uint32_t> expected_epoch,
                                StockSettingsRequest request = {}, StockSettings (*read_stock)(StockSettingsRequest) = nullptr);
  static bool save_wifi(const WifiCredentials &wifi, uint32_t expected_epoch);
  static bool save_slide_interval(uint32_t seconds, std::string_view paired_key);
  static bool save_photo_preset(PhotoPreset preset, std::string_view paired_key);
  static std::optional<PhotoFeedSettings> take_live_photo_feed(uint32_t revision);
  static bool save_playback(const char *json, bool *restart_required = nullptr);
  static UpdateResult update_playback(const cJSON *update, std::optional<uint32_t> expected_epoch = {});
  void publish_live_presentation() const;
  static bool reset(ResetMode mode);
  bool add_playback_fields(cJSON *object) const;
};

}  // namespace espframe::native
