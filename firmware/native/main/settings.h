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
#include "slideshow_interval.h"
#include "stock_settings.h"
#include "wifi_credentials.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct cJSON;

namespace espframe::native {

inline std::atomic<ScreenRotation> requested_screen_rotation{};
struct ScreenBrightness {
  uint8_t day{64};
  uint8_t night{64};
  bool operator==(const ScreenBrightness &) const = default;
};
inline std::atomic<ScreenBrightness> active_screen_brightness{};
inline std::atomic<uint32_t> active_screen_schedule{ScreenScheduleSettings{}.packed()};

inline constexpr const char *screen_rotation_name(ScreenRotation rotation) {
  switch (rotation) {
    case ScreenRotation::Clockwise: return "90";
    case ScreenRotation::UpsideDown: return "180";
    case ScreenRotation::Counterclockwise: return "270";
    case ScreenRotation::Normal: return "0";
  }
  return "0";
}

enum class ResetMode { Customization, Factory };
enum class PhotoFilterUpdate { KeepSource, Compose };

struct PhotoConnectionSettings {
  std::string immich_url;
  std::string immich_key;
  std::string prepared_slide_url;
  std::string prepared_slide_token;

  bool has_immich() const { return !immich_url.empty() && !immich_key.empty(); }
  bool has_prepared_slides() const { return !prepared_slide_url.empty() && !prepared_slide_token.empty(); }
  bool prepared_slides_need_time() const { return prepared_slide_url.starts_with("https://"); }
  bool operator==(const PhotoConnectionSettings &) const = default;
};

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
  ScreenToneSettings tone;
  // Present only in the live update that switches the worker's connection.
  // This is never included in the serialized photo settings.
  std::optional<PhotoConnectionSettings> pending_connection;

  bool needs_local_date() const {
    return metadata.needs_time() || capture_dates.needs_time() || photo_source.mode == PhotoSourceMode::Memories;
  }
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

struct StockUpdatePreferences {
  std::optional<bool> auto_update;
  std::optional<std::string> frequency;
  std::optional<bool> wifi_auto_update;
  bool operator==(const StockUpdatePreferences &) const = default;
};

struct WireguardSettings {
  bool enabled{};
  uint16_t port{51820};
  std::string endpoint;
  std::string address;
  std::string netmask{"255.255.255.255"};
  std::string allowed_ips;
  std::string peer_public_key;
  std::string private_key;
  bool operator==(const WireguardSettings &) const = default;

  bool add_public_fields(cJSON *object) const;
};

struct Settings : PhotoFeedSettings, PhotoConnectionSettings {
  enum class UpdateResult { Saved, SavedLive, Unchanged, Invalid, Stale, Failed };
  enum class UsbSaveResult { Saved, Rejected, WifiUnavailable, ImmichUnavailable, PairingUnavailable };

  std::string ssid;
  std::string password;
  std::string web_password;
  std::string home_assistant_key;
  std::string frame_name;
  bool home_assistant_key_set{};
  uint32_t reset_epoch{};
  uint32_t slide_seconds{15};
  uint32_t connection_timeout_seconds{default_connection_timeout};
  std::string transition{"Cross Fade"};
  std::string transition_speed{"Fast"};
  uint8_t brightness_day{64};
  std::optional<uint8_t> brightness_night;
  ScreenRotation screen_rotation{ScreenRotation::Normal};
  bool developer_features_enabled{};
  ClockSettings clock;
  ScreenScheduleSettings schedule;
  StockUpdatePreferences stock_update_preferences;
  WireguardSettings wireguard;

  uint8_t night_brightness() const { return brightness_night.value_or(brightness_day); }
  static constexpr size_t max_json_size = 6144;
  static uint32_t storage_revision();
  bool read();
  bool read_esphome_trial();
  static bool save(const char *json, std::optional<uint32_t> expected_epoch = {});
  static UsbSaveResult save_usb(const char *json, std::optional<uint32_t> expected_epoch,
                                StockSettingsRequest request = {}, StockSettings (*read_stock)(StockSettingsRequest) = nullptr);
  static bool save_wifi(const WifiCredentials &wifi, uint32_t expected_epoch);
  static bool save_slide_interval(uint32_t seconds, std::string_view paired_key);
  static bool save_photo_preset(PhotoPreset preset, std::string_view paired_key);
  static bool save_connection_timeout(uint32_t seconds, std::string_view paired_key);
  static bool save_device_language(uint8_t language, std::string_view paired_key);
  static bool expire_warm_tone_override(std::string_view timezone, const PhotoFeedSettings &active_feed);
  static std::optional<PhotoFeedSettings> take_live_photo_feed(uint32_t revision);
  static bool save_playback(const char *json, bool *restart_required = nullptr);
  static UpdateResult update_playback(const cJSON *update, std::optional<uint32_t> expected_epoch = {},
                                      PhotoFilterUpdate filter_update = PhotoFilterUpdate::KeepSource,
                                      std::optional<PhotoPreset> preset = {});
  void publish_live_presentation() const;
  static bool reset(ResetMode mode);
  bool add_playback_fields(cJSON *object, PhotoFilterUpdate filter_update = PhotoFilterUpdate::KeepSource) const;
  bool add_stock_update_preferences(cJSON *object) const;
};

}  // namespace espframe::native
