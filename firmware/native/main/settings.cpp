#include "settings.h"
#include "frame_identity_model.h"
#include "home_assistant_key.h"
#include "live_trial.h"
#include "slideshow_interval.h"

#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

namespace espframe::native {
namespace {

constexpr const char *partition = "native_config";
constexpr const char *store_namespace = "player";
constexpr const char *settings_key = "settings_v1";
constexpr size_t legacy_max_json_size = 3072;
std::mutex storage_mutex;

bool load_string(nvs_handle_t storage, const char *key, std::string &value, size_t maximum) {
  size_t size = 0;
  if (nvs_get_str(storage, key, nullptr, &size) != ESP_OK || size == 0 || size > maximum + 1)
    return false;
  value.resize(size);
  if (nvs_get_str(storage, key, value.data(), &size) != ESP_OK)
    return false;
  value.resize(size - 1);
  return true;
}

bool normalize_http_base(std::string &url) {
  const size_t host = url.starts_with("https://") ? 8 : url.starts_with("http://") ? 7 : 0;
  if (host == 0 || host >= url.size() || url.size() > 256 || url[host] == '/' ||
      url.find_first_of("@?#\\") != std::string::npos ||
      std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 32 || c >= 127; }))
    return false;
  while (url.back() == '/')
    url.pop_back();
  return true;
}

bool normalize(Settings &settings) {
  std::string name;
  if (!esphome::espframe::normalize_frame_name(settings.frame_name, name))
    return false;
  settings.frame_name = std::move(name);
  if (!settings.home_assistant_key.empty() && !decode_home_assistant_key(settings.home_assistant_key))
    return false;
  if (settings.ssid.empty())
    return settings.reset_epoch != 0 && settings.password.empty() && settings.web_password.empty() &&
           settings.immich_url.empty() && settings.immich_key.empty() && settings.prepared_slide_url.empty() &&
           settings.prepared_slide_token.empty() && settings.home_assistant_key.empty();
  if (settings.ssid.size() > 32 || settings.password.size() > 64 ||
      settings.web_password.size() > 64 || (!settings.web_password.empty() && settings.web_password.size() < 8) ||
      settings.slide_seconds < 2 || settings.slide_seconds > 86'400 ||
      settings.immich_key.size() > 256 ||
      std::any_of(settings.immich_key.begin(), settings.immich_key.end(),
                  [](unsigned char c) { return c <= 32 || c >= 127; }))
    return false;
  if (!settings.prepared_slide_url.empty()) {
    const auto &token = settings.prepared_slide_token;
    if (!normalize_http_base(settings.prepared_slide_url) || token.size() < 32 || token.size() > 256 ||
        token == settings.immich_key || std::any_of(token.begin(), token.end(),
                                                  [](unsigned char c) { return c <= 32 || c >= 127; }))
      return false;
  } else if (!settings.prepared_slide_token.empty()) {
    return false;
  }
  auto &url = settings.immich_url;
  if (url.empty() && settings.immich_key.empty())
    return !settings.has_prepared_slides();
  if (!settings.has_immich() || !normalize_http_base(url))
    return false;
  if (!url.ends_with("/api"))
    url += "/api";
  return true;
}

using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

Json parse(const char *json) {
  if (json == nullptr || std::strlen(json) > Settings::max_json_size)
    return {nullptr, cJSON_Delete};
  // cJSON strings cannot distinguish an escaped NUL from the end of a field.
  std::string_view input(json);
  for (size_t pos = input.find('\\'); pos != input.npos; pos = input.find('\\', pos + 2)) {
    if (input.substr(pos + 1, 5) == "u0000")
      return {nullptr, cJSON_Delete};
  }
  return {cJSON_ParseWithOpts(json, nullptr, true), cJSON_Delete};
}

bool read_field(const cJSON *root, const char *name, std::string &value, bool optional = false) {
  const cJSON *field = cJSON_GetObjectItemCaseSensitive(root, name);
  if (!field && optional)
    return true;
  const char *text = cJSON_GetStringValue(field);
  if (!text)
    return false;
  value = text;
  return true;
}

template <typename Integer>
bool read_integer(const cJSON *root, const char *name, double minimum, double maximum, Integer &value) {
  const auto *field = cJSON_GetObjectItemCaseSensitive(root, name);
  if (!field)
    return true;
  if (!cJSON_IsNumber(field) || !(field->valuedouble >= minimum && field->valuedouble <= maximum))
    return false;
  const auto integer = static_cast<Integer>(field->valuedouble);
  if (integer != field->valuedouble)
    return false;
  value = integer;
  return true;
}

bool read_flag(const cJSON *root, const char *name, bool &enabled) {
  const auto *field = cJSON_GetObjectItemCaseSensitive(root, name);
  if (!field)
    return true;
  if (!cJSON_IsBool(field))
    return false;
  enabled = cJSON_IsTrue(field);
  return true;
}

bool read_clock(const cJSON *root, ClockSettings &clock) {
  return read_flag(root, "show_clock", clock.show) && read_field(root, "clock_format", clock.format, true) &&
         (clock.format == "24 Hour" || clock.format == "12 Hour") &&
         read_field(root, "timezone", clock.timezone, true) && clock_timezone(clock.timezone) != nullptr;
}

bool read_schedule(const cJSON *root, ScreenScheduleSettings &schedule) {
  return read_flag(root, "schedule_enabled", schedule.enabled) &&
         read_integer(root, "schedule_on_hour", 0, 23, schedule.on_hour) &&
         read_integer(root, "schedule_off_hour", 0, 23, schedule.off_hour) &&
         read_integer(root, "schedule_wake_timeout", 10, 3600, schedule.wake_seconds);
}

bool read_tone(const cJSON *root, ScreenToneSettings &tone) {
  return read_flag(root, "base_tone_enabled", tone.base_enabled) &&
         read_integer(root, "base_tone", 0, 100, tone.base_amount) &&
         read_flag(root, "warm_tones_enabled", tone.night_enabled) &&
         read_integer(root, "warm_tone_intensity", 10, 100, tone.night_amount);
}

bool read_metadata(const cJSON *root, PhotoMetadataSettings &settings) {
  using Format = PhotoMetadataSettings;
  std::string date = settings.date_format();
  std::string taken = settings.taken_format();
  if (!read_flag(root, "photo_metadata_date_enabled", settings.show_date) ||
      !read_flag(root, "photo_metadata_location_enabled", settings.show_location) ||
      !read_field(root, "photo_metadata_date_format", date, true) ||
      !read_field(root, "photo_metadata_date_taken_format", taken, true) ||
      (date != Format::date_taken && date != Format::relative_date) ||
      (taken != Format::day_first && taken != Format::month_first))
    return false;
  settings.relative = date == Format::relative_date;
  settings.month_before_day = taken == Format::month_first;
  return true;
}

bool read_brightness(const cJSON *root, Settings &settings) {
  if (!read_integer(root, "brightness_day", 10, 100, settings.brightness_day))
    return false;
  if (cJSON_HasObjectItem(root, "brightness_night")) {
    uint8_t night{};
    if (!read_integer(root, "brightness_night", 10, 100, night))
      return false;
    settings.brightness_night = night;
  }
  return true;
}

bool read_display_mode(const cJSON *root, DisplayMode &mode) {
  const auto *field = cJSON_GetObjectItemCaseSensitive(root, "display_mode");
  if (!field)
    return true;
  const char *text = cJSON_GetStringValue(field);
  if (!text)
    return false;
  const std::string_view value{text};
  if (value == "Fill")
    mode = DisplayMode::Fill;
  else if (value == "Fit")
    mode = DisplayMode::Fit;
  else
    return false;
  return true;
}

bool read_rotation(const cJSON *root, ScreenRotation &rotation) {
  const auto *field = cJSON_GetObjectItemCaseSensitive(root, "screen_rotation");
  if (!field)
    return true;
  const char *text = cJSON_GetStringValue(field);
  if (!text)
    return false;
  const std::string_view value{text};
  if (value == "0")
    rotation = ScreenRotation::Normal;
  else if (value == "180")
    rotation = ScreenRotation::UpsideDown;
  else
    return false;
  return true;
}

bool read_orientation(const cJSON *root, PhotoOrientation &orientation) {
  const auto *field = cJSON_GetObjectItemCaseSensitive(root, "photo_orientation");
  if (!field)
    return true;
  const char *value = cJSON_GetStringValue(field);
  if (!value)
    return false;
  for (auto option : {PhotoOrientation::Any, PhotoOrientation::Portrait, PhotoOrientation::Landscape}) {
    if (std::string_view(value) == photo_orientation_name(option)) {
      orientation = option;
      return true;
    }
  }
  return false;
}

bool read_source(const cJSON *root, PhotoSource &source) {
  std::string mode = photo_source_name(source.mode);
  std::string window{memory_windows[source.memory_window]};
  if (!read_field(root, "photo_source", mode, true) || !read_field(root, "album_id", source.album_id, true) ||
      !read_field(root, "memories_window", window, true) || !read_flag(root, "memories_fallback", source.memory_fallback))
    return false;
  const auto match = std::find(memory_windows.begin(), memory_windows.end(), window);
  if (match == memory_windows.end())
    return false;
  source.memory_window = std::distance(memory_windows.begin(), match);
  for (char &c : source.album_id) {
    if (c >= 'A' && c <= 'F')
      c = static_cast<char>(c + ('a' - 'A'));
  }
  if ((!source.album_id.empty() || mode == "album") && !is_immich_id(source.album_id))
    return false;
  if (mode == "all")
    source.mode = PhotoSourceMode::Library;
  else if (mode == "favorites")
    source.mode = PhotoSourceMode::Favorites;
  else if (mode == "album")
    source.mode = PhotoSourceMode::Album;
  else if (mode == "memories")
    source.mode = PhotoSourceMode::Memories;
  else if (mode == "custom")
    source.mode = PhotoSourceMode::Custom;
  else
    return false;
  return true;
}

constexpr std::pair<const char *, bool ImmichFilterConfig::*> filter_flags[]{
    {"albums_enabled", &ImmichFilterConfig::albums_enabled}, {"people_enabled", &ImmichFilterConfig::people_enabled},
    {"tags_enabled", &ImmichFilterConfig::tags_enabled}, {"favorites_enabled", &ImmichFilterConfig::favorites_enabled},
    {"rating_enabled", &ImmichFilterConfig::rating_enabled}, {"location_enabled", &ImmichFilterConfig::location_enabled}};

constexpr std::pair<const char *, std::string ImmichFilterConfig::*> filter_lists[]{
    {"album_ids", &ImmichFilterConfig::album_ids}, {"person_ids", &ImmichFilterConfig::person_ids},
    {"tag_ids", &ImmichFilterConfig::tag_ids}, {"excluded_album_ids", &ImmichFilterConfig::excluded_album_ids},
    {"excluded_person_ids", &ImmichFilterConfig::excluded_person_ids}, {"excluded_tag_ids", &ImmichFilterConfig::excluded_tag_ids}};

constexpr std::pair<const char *, std::string ImmichFilterConfig::*> filter_places[]{
    {"filter_country", &ImmichFilterConfig::country}, {"filter_state", &ImmichFilterConfig::state},
    {"filter_city", &ImmichFilterConfig::city}};

constexpr std::array ratings{"Any", "1+", "2+", "3+", "4+", "5+"};

bool read_choice(const cJSON *root, const char *key, std::string &value,
                 std::initializer_list<std::string_view> choices) {
  return read_field(root, key, value, true) && std::find(choices.begin(), choices.end(), value) != choices.end();
}

bool normalize_filter_ids(std::string &ids) {
  if (ids.size() > 255)
    return false;
  std::string normalized;
  std::string_view remaining(ids);
  while (!remaining.empty()) {
    const auto separator = remaining.find(',');
    auto id = remaining.substr(0, separator);
    const auto first = id.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
      return false;
    id = id.substr(first, id.find_last_not_of(" \t\r\n") - first + 1);
    if (!normalized.empty())
      normalized += ',';
    const auto start = normalized.size();
    for (const char c : id)
      normalized += c >= 'A' && c <= 'F' ? static_cast<char>(c + ('a' - 'A')) : c;
    if (!is_immich_id(std::string_view(normalized).substr(start)))
      return false;
    if (separator == std::string_view::npos)
      break;
    remaining.remove_prefix(separator + 1);
    if (remaining.empty())
      return false;
  }
  ids = std::move(normalized);
  return true;
}

bool read_photo_filters(const cJSON *root, PhotoFilters &filters) {
  auto &criteria = filters.criteria;
  for (const auto &[key, member] : filter_flags) {
    if (!read_flag(root, key, criteria.*member))
      return false;
  }
  for (const auto &[key, member] : filter_lists) {
    if (!read_field(root, key, criteria.*member, true) || !normalize_filter_ids(criteria.*member))
      return false;
  }
  for (const auto &[key, member] : filter_places) {
    auto &value = criteria.*member;
    if (!read_field(root, key, value, true) || value.size() > 96 ||
        std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; }))
      return false;
  }
  std::string rating = ratings[criteria.minimum_rating];
  if (!read_field(root, "minimum_rating", rating, true))
    return false;
  const auto selected = std::find(ratings.begin(), ratings.end(), rating);
  if (selected == ratings.end())
    return false;
  criteria.minimum_rating = std::distance(ratings.begin(), selected);
  return read_choice(root, "album_order", filters.album_order, {"Random albums", "Album list order"}) &&
         read_choice(root, "album_matching", criteria.album_matching, {"Any selected album", "All selected albums"}) &&
         read_choice(root, "person_matching", criteria.person_matching, {"Any selected person", "All selected people"}) &&
         read_choice(root, "tag_matching", criteria.tag_matching, {"Any selected tag", "All selected tags"}) &&
         read_choice(root, "inclusion_matching", criteria.inclusion_matching,
                     {"Match all enabled groups", "Match any enabled group"}) &&
         read_choice(root, "favorite_mode", criteria.favorite_mode, {"Any", "Favorites only", "Exclude favorites"});
}

bool add_photo_filter_fields(cJSON *object, const PhotoFilters &filters) {
  const auto &criteria = filters.criteria;
  for (const auto &[key, member] : filter_flags) {
    if (!cJSON_AddBoolToObject(object, key, criteria.*member))
      return false;
  }
  for (const auto &[key, member] : filter_lists) {
    if (!cJSON_AddStringToObject(object, key, (criteria.*member).c_str()))
      return false;
  }
  for (const auto &[key, member] : filter_places) {
    if (!cJSON_AddStringToObject(object, key, (criteria.*member).c_str()))
      return false;
  }
  return cJSON_AddStringToObject(object, "album_order", filters.album_order.c_str()) &&
         cJSON_AddStringToObject(object, "album_matching", criteria.album_matching.c_str()) &&
         cJSON_AddStringToObject(object, "person_matching", criteria.person_matching.c_str()) &&
         cJSON_AddStringToObject(object, "tag_matching", criteria.tag_matching.c_str()) &&
         cJSON_AddStringToObject(object, "inclusion_matching", criteria.inclusion_matching.c_str()) &&
         cJSON_AddStringToObject(object, "favorite_mode", criteria.favorite_mode.c_str()) &&
         cJSON_AddStringToObject(object, "minimum_rating", ratings[criteria.minimum_rating]);
}

bool read_capture_dates(const cJSON *root, CaptureDateSettings &dates) {
  std::string mode = dates.mode_name();
  std::string unit = dates.unit_name();
  if (!read_flag(root, "date_filter_enabled", dates.enabled) || !read_field(root, "date_filter_mode", mode, true) ||
      !read_field(root, "date_from", dates.from, true) || !read_field(root, "date_to", dates.through, true) ||
      !read_integer(root, "relative_amount", 1, 120, dates.amount) || !read_field(root, "relative_unit", unit, true) ||
      (mode != CaptureDateSettings::fixed_mode && mode != CaptureDateSettings::relative_mode) ||
      (unit != "Months" && unit != "Years"))
    return false;
  if ((!dates.from.empty() && !parse_calendar_day(dates.from)) ||
      (!dates.through.empty() && !parse_calendar_day(dates.through)) ||
      (!dates.from.empty() && !dates.through.empty() && dates.from > dates.through))
    return false;
  dates.relative = mode == CaptureDateSettings::relative_mode;
  dates.years = unit == "Years";
  return true;
}

bool read_pairing(const cJSON *root, Settings &settings) {
  std::string range{portrait_pairing_ranges[settings.portrait_pairing_days]};
  std::string framing{pair_framing_name(settings.pair_framing)};
  if (!read_flag(root, "portrait_pairing", settings.portrait_pairing) ||
      !read_flag(root, "portrait_pairs_only", settings.portrait_pairs_only) ||
      !read_field(root, "portrait_pairing_range", range, true) ||
      !read_field(root, "portrait_pair_framing", framing, true))
    return false;
  const auto match = std::find(portrait_pairing_ranges.begin(), portrait_pairing_ranges.end(), range);
  if (match == portrait_pairing_ranges.end())
    return false;
  settings.portrait_pairing_days = std::distance(portrait_pairing_ranges.begin(), match);
  for (auto choice : {PairFraming::FollowPhotos, PairFraming::Fill, PairFraming::Fit}) {
    if (framing == pair_framing_name(choice)) {
      settings.pair_framing = choice;
      return true;
    }
  }
  return false;
}

bool read_playback(const cJSON *root, Settings &settings) {
  return read_integer(root, "slide_seconds", 2, 86'400, settings.slide_seconds) && read_pairing(root, settings) &&
         read_display_mode(root, settings.display_mode) && read_orientation(root, settings.photo_orientation) &&
         read_rotation(root, settings.screen_rotation) &&
         read_source(root, settings.photo_source) && read_photo_filters(root, settings.photo_filters) &&
         read_capture_dates(root, settings.capture_dates) &&
         read_clock(root, settings.clock) && read_brightness(root, settings) &&
         read_schedule(root, settings.schedule) && read_metadata(root, settings.metadata) && read_tone(root, settings.tone);
}

bool read_home_assistant_key(const cJSON *root, Settings &settings) {
  settings.home_assistant_key_set = cJSON_HasObjectItem(root, "home_assistant_key");
  return read_field(root, "home_assistant_key", settings.home_assistant_key, true);
}

bool parse_all(const cJSON *root, Settings &settings) {
  return cJSON_IsObject(root) && read_field(root, "ssid", settings.ssid) &&
         read_field(root, "password", settings.password) &&
         read_field(root, "immich_url", settings.immich_url) &&
         read_field(root, "immich_key", settings.immich_key) &&
         read_field(root, "prepared_slide_url", settings.prepared_slide_url, true) &&
         read_field(root, "prepared_slide_token", settings.prepared_slide_token, true) &&
         read_field(root, "web_password", settings.web_password, true) &&
         read_home_assistant_key(root, settings) &&
         read_field(root, "frame_name", settings.frame_name, true) &&
         read_integer(root, "reset_epoch", 0, std::numeric_limits<uint32_t>::max(), settings.reset_epoch) &&
         read_playback(root, settings) && normalize(settings);
}

Json load_document(bool *may_create = nullptr) {
  if (may_create)
    *may_create = false;
  if (nvs_flash_init_partition(partition) != ESP_OK)
    return {nullptr, cJSON_Delete};
  nvs_handle_t storage;
  const auto opened = nvs_open_from_partition(partition, store_namespace, NVS_READONLY, &storage);
  if (opened != ESP_OK) {
    if (may_create)
      *may_create = opened == ESP_ERR_NVS_NOT_FOUND;
    return {nullptr, cJSON_Delete};
  }
  std::string json;
  Json document(nullptr, cJSON_Delete);
  size_t size = 0;
  const auto blob_result = nvs_get_blob(storage, settings_key, nullptr, &size);
  if (blob_result == ESP_OK) {
    if (size > 0 && size <= Settings::max_json_size) {
      json.resize(size);
      if (nvs_get_blob(storage, settings_key, json.data(), &size) == ESP_OK && json.find('\0') == std::string::npos)
        document = parse(json.c_str());
    }
  } else if (load_string(storage, settings_key, json, Settings::max_json_size)) {
    document = parse(json.c_str());
  } else if (blob_result == ESP_ERR_NVS_NOT_FOUND &&
             nvs_get_str(storage, settings_key, nullptr, &size) == ESP_ERR_NVS_NOT_FOUND) {
    Settings legacy;
    if (load_string(storage, "ssid", legacy.ssid, 32) &&
        load_string(storage, "password", legacy.password, 64) &&
        load_string(storage, "immich_url", legacy.immich_url, 256) &&
        load_string(storage, "immich_key", legacy.immich_key, 256)) {
      document.reset(cJSON_CreateObject());
      if (!document || !cJSON_AddStringToObject(document.get(), "ssid", legacy.ssid.c_str()) ||
          !cJSON_AddStringToObject(document.get(), "password", legacy.password.c_str()) ||
          !cJSON_AddStringToObject(document.get(), "immich_url", legacy.immich_url.c_str()) ||
          !cJSON_AddStringToObject(document.get(), "immich_key", legacy.immich_key.c_str()))
        document.reset();
    } else if (may_create) {
      *may_create = nvs_get_str(storage, "ssid", nullptr, &size) == ESP_ERR_NVS_NOT_FOUND;
    }
  }
  nvs_close(storage);
  return document;
}

bool load(Settings &settings, bool *may_create = nullptr) {
  const auto document = load_document(may_create);
  return parse_all(document.get(), settings);
}

bool write(const char *json, bool clear_legacy) {
  const size_t length = std::strlen(json);
  if (length > Settings::max_json_size || nvs_flash_init_partition(partition) != ESP_OK)
    return false;
  nvs_handle_t storage;
  if (nvs_open_from_partition(partition, store_namespace, NVS_READWRITE, &storage) != ESP_OK)
    return false;
  bool ok = true;
  if (clear_legacy) {
    // Remove the pre-versioned settings so there are no credentials to fall back to.
    for (const char *key : {"ssid", "password", "immich_url", "immich_key"}) {
      const auto result = nvs_erase_key(storage, key);
      if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) {
        ok = false;
        break;
      }
    }
  }
  if (ok) {
    // Keep compact records readable by earlier native firmware. Larger records
    // share the same key and can span multiple NVS pages.
    const auto result = length <= legacy_max_json_size ? nvs_set_str(storage, settings_key, json)
                                                       : nvs_set_blob(storage, settings_key, json, length);
    ok = result == ESP_OK && nvs_commit(storage) == ESP_OK;
  }
  nvs_close(storage);
  return ok;
}

bool write_document(const cJSON *document, bool clear_legacy = false) {
  const std::unique_ptr<char, decltype(&cJSON_free)> serialized(cJSON_PrintUnformatted(document), cJSON_free);
  return serialized && write(serialized.get(), clear_legacy);
}

bool put_wifi(cJSON *document, const char *ssid, const char *password) {
  cJSON_DeleteItemFromObjectCaseSensitive(document, "ssid");
  cJSON_DeleteItemFromObjectCaseSensitive(document, "password");
  return cJSON_AddStringToObject(document, "ssid", ssid) && cJSON_AddStringToObject(document, "password", password);
}

bool put_epoch(cJSON *document, uint32_t epoch) {
  while (cJSON_GetObjectItemCaseSensitive(document, "reset_epoch"))
    cJSON_DeleteItemFromObjectCaseSensitive(document, "reset_epoch");
  return epoch == 0 || cJSON_AddNumberToObject(document, "reset_epoch", epoch) != nullptr;
}

Settings::UpdateResult store_settings(Settings &settings, bool clear_legacy = false) {
  using Result = Settings::UpdateResult;
  const Json updated(cJSON_CreateObject(), cJSON_Delete);
  if (!updated || !cJSON_AddStringToObject(updated.get(), "ssid", settings.ssid.c_str()) ||
      !cJSON_AddStringToObject(updated.get(), "password", settings.password.c_str()) ||
      !cJSON_AddStringToObject(updated.get(), "immich_key", settings.immich_key.c_str()) ||
      !cJSON_AddStringToObject(updated.get(), "prepared_slide_token", settings.prepared_slide_token.c_str()) ||
      !cJSON_AddStringToObject(updated.get(), "web_password", settings.web_password.c_str()) ||
      (settings.home_assistant_key_set &&
       !cJSON_AddStringToObject(updated.get(), "home_assistant_key", settings.home_assistant_key.c_str())) ||
      !put_epoch(updated.get(), settings.reset_epoch) || !settings.add_playback_fields(updated.get()))
    return Result::Failed;
  if (!settings.brightness_night)
    cJSON_DeleteItemFromObjectCaseSensitive(updated.get(), "brightness_night");
  if (!parse_all(updated.get(), settings))
    return Result::Invalid;
  return write_document(updated.get(), clear_legacy) ? Result::Saved : Result::Failed;
}

}  // namespace

#if SWIPE_TRIAL_IMMICH
bool Settings::read_esphome_trial() {
  Settings candidate;
  if (!read_live_trial_credentials(candidate) || !normalize(candidate))
    return false;
  *this = std::move(candidate);
  return true;
}
#endif

bool Settings::read() {
  Settings candidate;
  const std::lock_guard lock(storage_mutex);
  if (!load(candidate))
    return false;
  *this = std::move(candidate);
  return true;
}

bool Settings::save(const char *json, std::optional<uint32_t> expected_epoch) {
  return save_usb(json, expected_epoch) == UsbSaveResult::Saved;
}

Settings::UsbSaveResult Settings::save_usb(const char *json, std::optional<uint32_t> expected_epoch,
                                          std::optional<HomeAssistantKey> (*read_stock_pairing)()) {
  using Result = UsbSaveResult;
  const auto document = parse(json);
  if (!cJSON_IsObject(document.get()))
    return Result::Rejected;
  const bool new_key = cJSON_HasObjectItem(document.get(), "home_assistant_key");
  if (read_stock_pairing && new_key)
    return Result::Rejected;
  const std::lock_guard lock(storage_mutex);
  Settings saved;
  bool may_create = false;
  const bool configured = load(saved, read_stock_pairing ? &may_create : nullptr);
  const auto epoch = configured ? saved.reset_epoch : 0;
  if (expected_epoch && *expected_epoch != epoch)
    return Result::Rejected;
  // An explicit native key choice (including an empty key) blocks a later
  // import. A factory-reset epoch also blocks reusing old stock credentials.
  if (read_stock_pairing && ((!configured && !may_create) || epoch != 0 || saved.home_assistant_key_set))
    return Result::PairingUnavailable;
  if (!cJSON_HasObjectItem(document.get(), "ssid") && !cJSON_HasObjectItem(document.get(), "password")) {
    if (!configured || !put_wifi(document.get(), saved.ssid.c_str(), saved.password.c_str()))
      return Result::Rejected;
  }
  if (!cJSON_HasObjectItem(document.get(), "frame_name") && !saved.frame_name.empty() &&
      !cJSON_AddStringToObject(document.get(), "frame_name", saved.frame_name.c_str()))
    return Result::Rejected;
  if (!new_key && saved.home_assistant_key_set &&
      !cJSON_AddStringToObject(document.get(), "home_assistant_key", saved.home_assistant_key.c_str()))
    return Result::Rejected;
  if (!cJSON_HasObjectItem(document.get(), "portrait_pair_framing") && saved.pair_framing != PairFraming::FollowPhotos &&
      !cJSON_AddStringToObject(document.get(), "portrait_pair_framing", pair_framing_name(saved.pair_framing)))
    return Result::Rejected;
  Settings candidate;
  if (!put_epoch(document.get(), epoch) || !parse_all(document.get(), candidate) || !candidate.has_immich())
    return Result::Rejected;
  if (read_stock_pairing) {
    const auto key = read_stock_pairing();
    if (!key || !valid_home_assistant_key(*key))
      return Result::PairingUnavailable;
    if (!cJSON_AddStringToObject(document.get(), "home_assistant_key", encode_home_assistant_key(*key).c_str()))
      return Result::Rejected;
  }
  return write_document(document.get()) ? Result::Saved : Result::Rejected;
}

bool Settings::save_wifi(const WifiCredentials &wifi, uint32_t expected_epoch) {
  if (!wifi.configured() || wifi.ssid.back() != '\0' || wifi.password.back() != '\0')
    return false;
  const std::lock_guard lock(storage_mutex);
  bool may_create = false;
  auto document = load_document(&may_create);
  Settings existing;
  if (!parse_all(document.get(), existing)) {
    if (!may_create)
      return false;
    document.reset(cJSON_CreateObject());
    if (!document || !cJSON_AddStringToObject(document.get(), "immich_url", "") ||
        !cJSON_AddStringToObject(document.get(), "immich_key", ""))
      return false;
  }
  Settings candidate;
  return existing.reset_epoch == expected_epoch && put_wifi(document.get(), wifi.ssid.data(), wifi.password.data()) &&
         parse_all(document.get(), candidate) && write_document(document.get());
}

bool Settings::save_slide_interval(uint32_t seconds, std::string_view paired_key) {
  if (seconds < 2 || seconds > 86'400 || paired_key.empty())
    return false;
  const std::lock_guard lock(storage_mutex);
  Settings settings;
  if (!load(settings) || settings.home_assistant_key != paired_key)
    return false;
  if (settings.slide_seconds != seconds) {
    settings.slide_seconds = seconds;
    if (store_settings(settings) != UpdateResult::Saved)
      return false;
  }
  active_slideshow_interval.store(seconds, std::memory_order_relaxed);
  return true;
}

bool Settings::add_playback_fields(cJSON *object) const {
  return add_photo_filter_fields(object, photo_filters) &&
         cJSON_AddStringToObject(object, "frame_name", frame_name.c_str()) &&
         cJSON_AddStringToObject(object, "immich_url", immich_url.c_str()) &&
         cJSON_AddStringToObject(object, "prepared_slide_url", prepared_slide_url.c_str()) &&
         cJSON_AddNumberToObject(object, "slide_seconds", slide_seconds) &&
         cJSON_AddNumberToObject(object, "brightness_day", brightness_day) &&
         cJSON_AddNumberToObject(object, "brightness_night", night_brightness()) &&
         cJSON_AddBoolToObject(object, "schedule_enabled", schedule.enabled) &&
         cJSON_AddNumberToObject(object, "schedule_on_hour", schedule.on_hour) &&
         cJSON_AddNumberToObject(object, "schedule_off_hour", schedule.off_hour) &&
         cJSON_AddNumberToObject(object, "schedule_wake_timeout", schedule.wake_seconds) &&
         cJSON_AddBoolToObject(object, "base_tone_enabled", tone.base_enabled) &&
         cJSON_AddNumberToObject(object, "base_tone", tone.base_amount) &&
         cJSON_AddBoolToObject(object, "warm_tones_enabled", tone.night_enabled) &&
         cJSON_AddNumberToObject(object, "warm_tone_intensity", tone.night_amount) &&
         cJSON_AddBoolToObject(object, "portrait_pairing", portrait_pairing) &&
         cJSON_AddBoolToObject(object, "portrait_pairs_only", portrait_pairs_only) &&
         cJSON_AddStringToObject(object, "portrait_pairing_range", portrait_pairing_ranges[portrait_pairing_days].data()) &&
         cJSON_AddStringToObject(object, "portrait_pair_framing", pair_framing_name(pair_framing)) &&
         cJSON_AddStringToObject(object, "display_mode", display_mode_name(display_mode)) &&
         cJSON_AddStringToObject(object, "photo_orientation", photo_orientation_name(photo_orientation)) &&
         cJSON_AddStringToObject(object, "screen_rotation", screen_rotation_name(screen_rotation)) &&
         cJSON_AddStringToObject(object, "photo_source", photo_source_name(photo_source.mode)) &&
         cJSON_AddStringToObject(object, "album_id", photo_source.album_id.c_str()) &&
         cJSON_AddStringToObject(object, "memories_window", memory_windows[photo_source.memory_window].data()) &&
         cJSON_AddBoolToObject(object, "memories_fallback", photo_source.memory_fallback) &&
         cJSON_AddBoolToObject(object, "date_filter_enabled", capture_dates.enabled) &&
         cJSON_AddStringToObject(object, "date_filter_mode", capture_dates.mode_name()) &&
         cJSON_AddStringToObject(object, "date_from", capture_dates.from.c_str()) &&
         cJSON_AddStringToObject(object, "date_to", capture_dates.through.c_str()) &&
         cJSON_AddNumberToObject(object, "relative_amount", capture_dates.amount) &&
         cJSON_AddStringToObject(object, "relative_unit", capture_dates.unit_name()) &&
         cJSON_AddBoolToObject(object, "photo_metadata_date_enabled", metadata.show_date) &&
         cJSON_AddBoolToObject(object, "photo_metadata_location_enabled", metadata.show_location) &&
         cJSON_AddStringToObject(object, "photo_metadata_date_format", metadata.date_format()) &&
         cJSON_AddStringToObject(object, "photo_metadata_date_taken_format", metadata.taken_format()) &&
         cJSON_AddBoolToObject(object, "show_clock", clock.show) &&
         cJSON_AddStringToObject(object, "clock_format", clock.format.c_str()) &&
         cJSON_AddStringToObject(object, "timezone", clock.timezone.c_str());
}

bool Settings::save_playback(const char *json) {
  const auto update = parse(json);
  if (!cJSON_HasObjectItem(update.get(), "immich_url"))
    return false;
  std::optional<uint32_t> expected_epoch;
  if (cJSON_HasObjectItem(update.get(), "reset_epoch")) {
    uint32_t epoch{};
    if (!read_integer(update.get(), "reset_epoch", 0, std::numeric_limits<uint32_t>::max(), epoch))
      return false;
    expected_epoch = epoch;
  }
  return update_playback(update.get(), expected_epoch) == UpdateResult::Saved;
}

Settings::UpdateResult Settings::update_playback(const cJSON *update, std::optional<uint32_t> expected_epoch) {
  std::string replacement_key;
  std::string replacement_token;
  const std::lock_guard lock(storage_mutex);
  Settings settings;
  if (!cJSON_IsObject(update) || !load(settings) || settings.web_password.empty())
    return UpdateResult::Failed;
  if (expected_epoch && *expected_epoch != settings.reset_epoch)
    return UpdateResult::Stale;
  if (!update->child)
    return UpdateResult::Saved;
  const auto previous_prepared_url = settings.prepared_slide_url;
  if (!read_field(update, "immich_url", settings.immich_url, true) ||
      !read_field(update, "immich_key", replacement_key, true) ||
      !read_field(update, "frame_name", settings.frame_name, true) ||
      !read_field(update, "prepared_slide_url", settings.prepared_slide_url, true) ||
      !read_field(update, "prepared_slide_token", replacement_token, true) || !read_playback(update, settings))
    return UpdateResult::Invalid;
  if (!replacement_key.empty())
    settings.immich_key = replacement_key;
  if (settings.prepared_slide_url.empty()) {
    if (!replacement_token.empty())
      return UpdateResult::Invalid;
    settings.prepared_slide_token.clear();
  } else {
    if (!normalize_http_base(settings.prepared_slide_url) ||
        (settings.prepared_slide_url != previous_prepared_url && replacement_token.empty()))
      return UpdateResult::Invalid;
    if (!replacement_token.empty())
      settings.prepared_slide_token = std::move(replacement_token);
  }
  return store_settings(settings);
}

bool Settings::reset(ResetMode mode) {
  const std::lock_guard lock(storage_mutex);
  Settings saved;
  if (!load(saved) || saved.web_password.empty())
    return false;
  Settings reset;
  if (mode == ResetMode::Customization) {
    reset.ssid = std::move(saved.ssid);
    reset.password = std::move(saved.password);
    reset.web_password = std::move(saved.web_password);
    reset.home_assistant_key = std::move(saved.home_assistant_key);
    reset.home_assistant_key_set = saved.home_assistant_key_set;
  }
  reset.reset_epoch = saved.reset_epoch == std::numeric_limits<uint32_t>::max() ? 1 : saved.reset_epoch + 1;
  return store_settings(reset, mode == ResetMode::Factory) == UpdateResult::Saved;
}

}  // namespace espframe::native
