#include "settings.h"
#if NATIVE_LEGACY_MIGRATION
#include "esphome_v1_layout.h"
#include "legacy_migration_storage.h"
#endif
#include "frame_identity_model.h"
#include "home_assistant_key.h"
#include "i18n.h"
#include "live_trial.h"
#include "ntp_server_names.h"
#include "playback_transition.h"
#include "slideshow_interval.h"
#include "slideshow_transition.h"
#include "wireguard_helpers.h"

#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

namespace espframe::native {
namespace {

#if NATIVE_LEGACY_MIGRATION
constexpr const char *partition = "nvs";
#else
constexpr const char *partition = "native_config";
#endif
constexpr const char *store_namespace = "player";
constexpr const char *settings_key = "settings_v1";
constexpr size_t legacy_max_json_size = 3072;
// Apply only during setup. Missing fields in older saved records still use
// the fallback values declared by Settings.
constexpr char new_setup_preferences[] = R"json({
  "portrait_pairing": true,
  "portrait_pairs_only": true,
  "portrait_pairing_range": "Within 2 Days",
  "portrait_pair_framing": "Follow photo framing",
  "portrait_pair_divider": "Black",
  "photo_orientation": "Any",
  "display_mode": "Fill",
  "transition": "Cross Fade",
  "transition_speed": "Fast",
  "photo_metadata_date_enabled": false,
  "photo_metadata_location_enabled": false,
  "show_clock": false,
  "clock_format": "24 Hour",
  "timezone": "Europe/London (GMT+0)",
  "brightness_day": 100,
  "brightness_night": 70,
  "base_tone_enabled": true,
  "base_tone": 23,
  "warm_tones_enabled": true,
  "warm_tone_intensity": 64,
  "warm_tone_override": true
})json";
constexpr std::array ntp_fields{"ntp_server_1", "ntp_server_2", "ntp_server_3"};
std::mutex storage_mutex;
std::atomic<uint32_t> revision{};
std::optional<PhotoFeedSettings> published_feed;

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

bool valid_immich_key(std::string_view key) {
  return key.size() <= 256 && std::none_of(key.begin(), key.end(), [](unsigned char c) { return c <= 32 || c >= 127; });
}

bool normalize(Settings &settings) {
  if (portrait_screen(settings.screen_rotation)) {
    if (settings.developer_features_enabled)
      settings.portrait_pairing = false;
    else
      settings.screen_rotation = ScreenRotation::Normal;
  }
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
      settings.slide_seconds < 2 || settings.slide_seconds > 86'400 || !valid_immich_key(settings.immich_key))
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
  if (url.empty())
    return settings.immich_key.empty() && !settings.has_prepared_slides();
  if (!normalize_http_base(url) || (settings.has_prepared_slides() && !settings.has_immich()))
    return false;
  if (url.find('/', url.find("://") + 3) == std::string::npos || !url.ends_with("/api"))
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

bool read_wireguard_fields(const cJSON *root, WireguardSettings &config) {
  struct Field {
    const char *key;
    std::string WireguardSettings::*member;
    bool (*valid)(const std::string &);
  };
  constexpr Field fields[]{
      {"wireguard_endpoint", &WireguardSettings::endpoint, espframe_is_valid_wireguard_endpoint},
      {"wireguard_address", &WireguardSettings::address, espframe_is_valid_wireguard_address},
      {"wireguard_netmask", &WireguardSettings::netmask, espframe_is_valid_ipv4_netmask},
      {"wireguard_peer_public_key", &WireguardSettings::peer_public_key, espframe_is_valid_wireguard_key},
      {"wireguard_private_key", &WireguardSettings::private_key, espframe_is_valid_wireguard_key},
  };
  for (const auto &field : fields) {
    if (!cJSON_HasObjectItem(root, field.key))
      continue;
    auto &value = config.*field.member;
    if (!read_field(root, field.key, value))
      return false;
    value = espframe_wireguard_trim(value);
    if (!value.empty() && !field.valid(value))
      return false;
  }
  if (cJSON_HasObjectItem(root, "wireguard_allowed_ips")) {
    if (!read_field(root, "wireguard_allowed_ips", config.allowed_ips) || config.allowed_ips.size() > 100)
      return false;
    config.allowed_ips = espframe_wireguard_trim(config.allowed_ips);
    if (!config.allowed_ips.empty()) {
      config.allowed_ips = espframe_normalize_wireguard_allowed_ips(config.allowed_ips);
      if (config.allowed_ips.empty())
        return false;
    }
  }
  return read_flag(root, "wireguard_enabled", config.enabled) &&
         read_integer(root, "wireguard_port", 1, 65535, config.port);
}

bool read_saved_wireguard(const cJSON *root, WireguardSettings &config) {
  const auto *saved = cJSON_GetObjectItemCaseSensitive(root, "wireguard");
  return !saved || (cJSON_IsObject(saved) && read_wireguard_fields(saved, config));
}

bool add_saved_wireguard(cJSON *root, const WireguardSettings &config) {
  if (config == WireguardSettings{})
    return true;
  auto *saved = cJSON_AddObjectToObject(root, "wireguard");
  return saved && config.add_public_fields(saved) &&
         cJSON_AddStringToObject(saved, "wireguard_private_key", config.private_key.c_str());
}

bool read_stock_update_preferences(const cJSON *root, StockUpdatePreferences &preferences) {
  const auto *fields = cJSON_GetObjectItemCaseSensitive(root, "firmware_updates");
  if (!fields)
    return true;
  if (!cJSON_IsObject(fields))
    return false;
  const auto read_optional_flag = [&](const char *key, std::optional<bool> &value) {
    if (!cJSON_HasObjectItem(fields, key))
      return true;
    bool enabled{};
    if (!read_flag(fields, key, enabled))
      return false;
    value = enabled;
    return true;
  };
  if (cJSON_HasObjectItem(fields, "update_frequency")) {
    constexpr std::string_view frequencies[]{"Hourly", "Daily", "Weekly", "Monthly"};
    std::string frequency;
    if (!read_field(fields, "update_frequency", frequency) ||
        std::ranges::find(frequencies, frequency) == std::end(frequencies))
      return false;
    preferences.frequency = std::move(frequency);
  }
  return read_optional_flag("auto_update", preferences.auto_update) &&
         read_optional_flag("wifi_auto_update", preferences.wifi_auto_update);
}

bool read_clock(const cJSON *root, ClockSettings &clock) {
  for (size_t i = 0; i < ntp_fields.size(); ++i) {
    auto &server = clock.ntp_servers[i];
    if (!read_field(root, ntp_fields[i], server, true) || !espframe_is_valid_ntp_server(server))
      return false;
    server = espframe_trim_ntp_server(server);
  }
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
         read_integer(root, "warm_tone_intensity", 10, 100, tone.night_amount) &&
         read_flag(root, "warm_tone_override", tone.night_override);
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
  if (const auto *field = cJSON_GetObjectItemCaseSensitive(root, "device_language")) {
    const auto *code = cJSON_GetStringValue(field);
    const auto language = code ? find_espframe_language(code) : espframe_i18n_catalogue::LANGUAGE_COUNT;
    if (language == espframe_i18n_catalogue::LANGUAGE_COUNT)
      return false;
    settings.language = static_cast<uint8_t>(language);
  }
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
  else if (value == "90")
    rotation = ScreenRotation::Clockwise;
  else if (value == "180")
    rotation = ScreenRotation::UpsideDown;
  else if (value == "270")
    rotation = ScreenRotation::Counterclockwise;
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

constexpr std::array filter_label_fields{"album_labels", "person_labels", "tag_labels",
    "excluded_album_labels", "excluded_person_labels", "excluded_tag_labels"};

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
  for (size_t i = 0; i < filter_label_fields.size(); ++i) {
    auto &labels = filters.labels[i];
    // Stock settings and backups can use either JSON text or comma-separated labels.
    if (!read_field(root, filter_label_fields[i], labels, true) || labels.size() > 255)
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

bool add_photo_filter_fields(cJSON *object, const PhotoFilters &filters, bool include_empty_labels) {
  const auto &criteria = filters.criteria;
  for (const auto &[key, member] : filter_flags) {
    if (!cJSON_AddBoolToObject(object, key, criteria.*member))
      return false;
  }
  for (const auto &[key, member] : filter_lists) {
    if (!cJSON_AddStringToObject(object, key, (criteria.*member).c_str()))
      return false;
  }
  for (size_t i = 0; i < filter_label_fields.size(); ++i) {
    if ((include_empty_labels || !filters.labels[i].empty()) &&
        !cJSON_AddStringToObject(object, filter_label_fields[i], filters.labels[i].c_str()))
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
  std::string range{portrait_pairing_ranges[settings.portrait_pairing_range_index]};
  std::string framing{pair_framing_name(settings.pair_framing)};
  std::string divider{pair_divider_name(settings.pair_divider)};
  if (!read_flag(root, "portrait_pairing", settings.portrait_pairing) ||
      !read_flag(root, "portrait_pairs_only", settings.portrait_pairs_only) ||
      !read_field(root, "portrait_pairing_range", range, true) ||
      !read_field(root, "portrait_pair_framing", framing, true) ||
      !read_field(root, "portrait_pair_divider", divider, true) ||
      (divider != "Black" && divider != "White"))
    return false;
  const auto match = std::find(portrait_pairing_ranges.begin(), portrait_pairing_ranges.end(), range);
  if (match == portrait_pairing_ranges.end())
    return false;
  settings.portrait_pairing_range_index = std::distance(portrait_pairing_ranges.begin(), match);
  settings.pair_divider = divider == "White" ? PairDivider::White : PairDivider::Black;
  for (auto choice : {PairFraming::FollowPhotos, PairFraming::Fill, PairFraming::Fit}) {
    if (framing == pair_framing_name(choice)) {
      settings.pair_framing = choice;
      return true;
    }
  }
  return false;
}

bool read_transition(const cJSON *root, Settings &settings) {
  using namespace esphome::espframe;
  return read_field(root, "transition", settings.transition, true) &&
         read_field(root, "transition_speed", settings.transition_speed, true) &&
         (settings.transition == "None" || slide_transition_is_random(settings.transition) ||
          parse_slide_transition(settings.transition) != SlideTransition::NONE) &&
         (settings.transition_speed == "Fast" || settings.transition_speed == "Normal" ||
          settings.transition_speed == "Slow");
}

bool read_connection_timeout(const cJSON *root, uint32_t &seconds) {
  const auto *field = cJSON_GetObjectItemCaseSensitive(root, "conn_timeout");
  if (!field)
    return true;
  const auto *label = cJSON_GetStringValue(field);
  const auto timeout = label ? interval_seconds(label) : 0;
  if (!valid_connection_timeout(timeout) || interval_label(timeout) != label)
    return false;
  seconds = timeout;
  return true;
}

bool read_playback(const cJSON *root, Settings &settings) {
  return read_integer(root, "slide_seconds", 2, 86'400, settings.slide_seconds) && read_pairing(root, settings) &&
         read_flag(root, "slideshow_progress_ring_enabled", settings.slideshow_progress_ring_enabled) &&
         read_transition(root, settings) && read_connection_timeout(root, settings.connection_timeout_seconds) &&
         read_display_mode(root, settings.display_mode) && read_orientation(root, settings.photo_orientation) &&
         read_rotation(root, settings.screen_rotation) &&
         read_flag(root, "developer_features_enabled", settings.developer_features_enabled) &&
         read_source(root, settings.photo_source) && read_photo_filters(root, settings.photo_filters) &&
         read_capture_dates(root, settings.capture_dates) &&
         read_clock(root, settings.clock) && read_brightness(root, settings) &&
         read_schedule(root, settings.schedule) && read_metadata(root, settings.metadata) && read_tone(root, settings.tone);
}

bool read_home_assistant_key(const cJSON *root, Settings &settings) {
  settings.home_assistant_key_set = cJSON_HasObjectItem(root, "home_assistant_key");
  return read_field(root, "home_assistant_key", settings.home_assistant_key, true);
}

bool read_saved_photo_preset(const cJSON *root, PhotoSource &source) {
  source.legacy_preset.reset();
  const auto *saved = cJSON_GetObjectItemCaseSensitive(root, "legacy_photo_preset");
  if (!saved)
    return true;
  const auto *name = cJSON_GetStringValue(saved);
  const auto preset = name ? parse_photo_preset(name) : std::nullopt;
  if (!preset || (*preset != PhotoPreset::Person && *preset != PhotoPreset::Tag && *preset != PhotoPreset::Album) ||
      source.mode != PhotoSourceMode::Custom)
    return false;
  source.legacy_preset = preset;
  return true;
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
         read_playback(root, settings) && read_saved_photo_preset(root, settings.photo_source) &&
         read_stock_update_preferences(root, settings.stock_update_preferences) &&
         read_saved_wireguard(root, settings.wireguard) && normalize(settings);
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
  if (!serialized || !write(serialized.get(), clear_legacy))
    return false;
  revision.fetch_add(1, std::memory_order_release);
  return true;
}

bool put_wifi(cJSON *document, const char *ssid, const char *password) {
  cJSON_DeleteItemFromObjectCaseSensitive(document, "ssid");
  cJSON_DeleteItemFromObjectCaseSensitive(document, "password");
  return cJSON_AddStringToObject(document, "ssid", ssid) && cJSON_AddStringToObject(document, "password", password);
}

bool fill_setup_preferences(cJSON *document, const cJSON *previous = nullptr) {
  const auto defaults = parse(new_setup_preferences);
  if (!defaults)
    return false;
  const cJSON *preference;
  cJSON_ArrayForEach(preference, defaults.get()) {
    if (cJSON_HasObjectItem(document, preference->string))
      continue;
    const auto *value = previous ? cJSON_GetObjectItemCaseSensitive(previous, preference->string) : preference;
    if (!value)
      continue;
    Json copy(cJSON_Duplicate(value, true), cJSON_Delete);
    if (!copy || !cJSON_AddItemToObject(document, preference->string, copy.get()))
      return false;
    copy.release();
  }
  return true;
}

bool put_epoch(cJSON *document, uint32_t epoch) {
  while (cJSON_GetObjectItemCaseSensitive(document, "reset_epoch"))
    cJSON_DeleteItemFromObjectCaseSensitive(document, "reset_epoch");
  return epoch == 0 || cJSON_AddNumberToObject(document, "reset_epoch", epoch) != nullptr;
}

bool is_composable_filter(std::string_view key) {
  const auto matches = [key](const auto &field) { return key == field.first; };
  constexpr std::string_view options[]{
      "minimum_rating", "album_matching", "person_matching", "tag_matching", "inclusion_matching", "favorite_mode"};
  return std::ranges::any_of(filter_flags, matches) || std::ranges::any_of(filter_lists, matches) ||
         std::ranges::any_of(filter_places, matches) || std::ranges::find(options, key) != std::end(options);
}

PhotoFilters shared_photo_filters(const PhotoFeedSettings &settings) {
  auto filters = settings.photo_filters;
  if (settings.photo_source.mode == PhotoSourceMode::Album) {
    if (filters.criteria.album_ids.empty()) {
      filters.criteria.album_ids = settings.photo_source.album_id;
      filters.criteria.album_matching = "Any selected album";
    }
    filters.criteria.albums_enabled = true;
  } else if (settings.photo_source.mode == PhotoSourceMode::Favorites) {
    filters.criteria.favorites_enabled = true;
    filters.criteria.favorite_mode = "Favorites only";
  }
  return filters;
}

void apply_photo_preset(PhotoFeedSettings &settings, PhotoPreset preset) {
  auto &source = settings.photo_source;
  auto &criteria = settings.photo_filters.criteria;
  source.legacy_preset.reset();
  if (preset == PhotoPreset::Memories) {
    source.mode = PhotoSourceMode::Memories;
  } else if (preset == PhotoPreset::Custom) {
    source.mode = PhotoSourceMode::Custom;
  } else {
    // Use the same preset behavior as the stock selector. Keep the saved
    // inclusion lists so another preset can use them later.
    criteria.albums_enabled = preset == PhotoPreset::Album;
    criteria.people_enabled = preset == PhotoPreset::Person;
    criteria.tags_enabled = preset == PhotoPreset::Tag;
    criteria.favorites_enabled = preset == PhotoPreset::Favorites;
    criteria.rating_enabled = criteria.location_enabled = false;
    criteria.inclusion_matching = "Match all enabled groups";
    criteria.album_matching = "Any selected album";
    criteria.person_matching = "Any selected person";
    criteria.tag_matching = "Any selected tag";
    criteria.favorite_mode = criteria.favorites_enabled ? "Favorites only" : "Any";
    criteria.minimum_rating = 0;
    criteria.country.clear();
    criteria.state.clear();
    criteria.city.clear();
    criteria.excluded_album_ids.clear();
    criteria.excluded_person_ids.clear();
    criteria.excluded_tag_ids.clear();
    for (size_t i = 3; i < filter_label_fields.size(); ++i)
      settings.photo_filters.labels[i].clear();
    if (preset == PhotoPreset::All) {
      source.mode = PhotoSourceMode::Library;
    } else if (preset == PhotoPreset::Favorites) {
      source.mode = PhotoSourceMode::Favorites;
    } else if (preset == PhotoPreset::Album && !source.album_id.empty() &&
               (criteria.album_ids.empty() || criteria.album_ids == source.album_id)) {
      source.mode = PhotoSourceMode::Album;
      criteria.album_ids = source.album_id;
    } else {
      source.mode = PhotoSourceMode::Custom;
      source.legacy_preset = preset;
    }
  }
}

bool changes_photo_source(std::string_view key) {
  return key == "photo_source" || key == "album_id" || key == "album_order" || is_composable_filter(key);
}

bool changes_photo_selection(std::string_view key) {
  constexpr std::string_view options[]{
      "memories_window", "memories_fallback", "date_filter_enabled", "date_filter_mode", "date_from", "date_to",
      "relative_amount", "relative_unit", "photo_orientation", "portrait_pairing", "portrait_pairs_only",
      "portrait_pairing_range"};
  return changes_photo_source(key) || std::ranges::find(options, key) != std::end(options);
}

bool changes_photo_presentation(std::string_view key) {
  constexpr std::string_view options[]{
      "display_mode", "portrait_pair_framing", "portrait_pair_divider", "photo_metadata_date_enabled",
      "photo_metadata_location_enabled",
      "photo_metadata_date_format", "photo_metadata_date_taken_format", "device_language", "base_tone_enabled", "base_tone",
      "warm_tones_enabled", "warm_tone_intensity", "warm_tone_override", "show_clock", "timezone"};
  return std::ranges::find(options, key) != std::end(options);
}

bool changes_live_presentation(std::string_view key) {
  return key == "slide_seconds" || key == "slideshow_progress_ring_enabled" || key == "clock_format" ||
         key == "transition" || key == "transition_speed" ||
         key == "conn_timeout" || key == "screen_rotation" || key == "brightness_day" || key == "brightness_night" ||
         key == "schedule_enabled" || key == "schedule_on_hour" || key == "schedule_off_hour" ||
         key == "schedule_wake_timeout" || key == "show_clock" || key == "timezone" ||
         std::ranges::find(ntp_fields, key) != std::end(ntp_fields);
}

void publish_photo_feed(PhotoFeedSettings &&feed, bool selection_changed) {
  active_photo_preset.store(feed.photo_source.preset(), std::memory_order_relaxed);
  active_warm_tone_override.store(feed.tone.night_override, std::memory_order_relaxed);
  active_device_language.store(feed.metadata.language, std::memory_order_relaxed);
  if (published_feed && !feed.pending_connection)
    feed.pending_connection = std::move(published_feed->pending_connection);
  published_feed = std::move(feed);
  const auto revision = photo_source_revision.load(std::memory_order_relaxed) + 1;
  // The last selection revision survives if another save publishes a
  // framing change before the player has picked up the new selection.
  if (selection_changed)
    photo_selection_revision.store(revision, std::memory_order_relaxed);
  photo_source_revision.store(revision, std::memory_order_release);
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
      !put_epoch(updated.get(), settings.reset_epoch) || !settings.add_playback_fields(updated.get()) ||
      !settings.add_stock_update_preferences(updated.get()) || !add_saved_wireguard(updated.get(), settings.wireguard))
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

#if NATIVE_LEGACY_MIGRATION
bool Settings::import_esphome_migration() {
  using namespace espframe_native_ota;
  const auto *running = esp_ota_get_running_partition();
  const auto *previous = other_esphome_slot(running);
  if (!previous)
    return false;
  const bool first_boot = is_app_in_ota_state(running, ESP_OTA_IMG_PENDING_VERIFY);
  const bool migrating = first_boot && partition_has_project(previous, NATIVE_HOSTNAME);
  if (first_boot && ((!migrating && !partition_has_project(previous, "espframe_legacy_" NATIVE_BOARD)) ||
                     !is_app_in_ota_state(previous, ESP_OTA_IMG_VALID) || !esp_ota_check_rollback_is_possible()))
    return false;

  const std::lock_guard lock(storage_mutex);
  Settings candidate;
  bool may_create = false;
  if (!migrating)
    return load(candidate);
  // Rebuild from the fresh bridge snapshot for each pending installation.
  // A previously failed trial may have written an older native document.
  if (!load(candidate, &may_create) && !may_create)
    return false;
  auto imported = convert_esphome_migration(read_migration_snapshot(),
      read_stock_settings({.wifi = true, .immich = true, .pairing = true}));
  return imported && store_settings(*imported) == UpdateResult::Saved;
}
#endif

std::optional<Settings> Settings::convert_esphome_migration(const std::string &snapshot, const StockSettings &stock) {
  const auto document = parse(snapshot.c_str());
  if (!cJSON_IsObject(document.get()))
    return std::nullopt;

  const auto *interval = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(document.get(), "interval"));
  const auto seconds = interval ? interval_seconds(interval) : 0;
  const auto *source = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(document.get(), "photo_source"));
  const auto preset = source ? parse_photo_preset(source) : std::nullopt;
  const auto *url = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(document.get(), "immich_url"));
  if (seconds < 2 || seconds > 86'400 || !preset || !stock.wifi || !stock.wifi->configured() ||
      !url || stock.immich_url != url || stock.immich_key.empty() ||
      cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(document.get(), "wireguard_enabled")))
    return std::nullopt;
  cJSON_DeleteItemFromObjectCaseSensitive(document.get(), "photo_source");
  if (!put_wifi(document.get(), stock.wifi->ssid.data(), stock.wifi->password.data()) ||
      !cJSON_AddStringToObject(document.get(), "immich_key", stock.immich_key.c_str()) ||
      !cJSON_AddNumberToObject(document.get(), "slide_seconds", seconds) ||
      (stock.frame_name && !cJSON_AddStringToObject(document.get(), "frame_name", stock.frame_name->c_str())) ||
      (stock.pairing && !cJSON_AddStringToObject(document.get(), "home_assistant_key", encode_home_assistant_key(*stock.pairing).c_str())))
    return std::nullopt;
  auto *updates = cJSON_AddObjectToObject(document.get(), "firmware_updates");
  if (!updates)
    return std::nullopt;
  for (const auto &[stock_key, native_key] : {std::pair{"auto_update", "auto_update"},
                                             std::pair{"update_frequency", "update_frequency"},
                                             std::pair{"c6_auto_update", "wifi_auto_update"}}) {
    auto *item = cJSON_DetachItemFromObjectCaseSensitive(document.get(), stock_key);
    if (!item || !cJSON_AddItemToObject(updates, native_key, item)) {
      cJSON_Delete(item);
      return std::nullopt;
    }
  }
  Settings imported;
  if (!parse_all(document.get(), imported) || !imported.has_immich())
    return std::nullopt;
  imported.pair_framing = PairFraming::Fill;
  // Legacy presets carry their filters alongside the selected label.
  // Preserve the saved criteria rather than resetting them to preset defaults.
  switch (*preset) {
    case PhotoPreset::All: imported.photo_source.mode = PhotoSourceMode::Library; break;
    case PhotoPreset::Favorites: imported.photo_source.mode = PhotoSourceMode::Favorites; break;
    case PhotoPreset::Memories: imported.photo_source.mode = PhotoSourceMode::Memories; break;
    case PhotoPreset::Album:
    case PhotoPreset::Person:
    case PhotoPreset::Tag:
      imported.photo_source.legacy_preset = *preset;
      [[fallthrough]];
    case PhotoPreset::Custom: imported.photo_source.mode = PhotoSourceMode::Custom; break;
  }
  return imported;
}

bool Settings::read() {
  Settings candidate;
  const std::lock_guard lock(storage_mutex);
  if (!load(candidate))
    return false;
  *this = std::move(candidate);
  return true;
}

uint32_t Settings::storage_revision() {
  return revision.load(std::memory_order_acquire);
}

bool WireguardSettings::add_public_fields(cJSON *object) const {
  return cJSON_AddBoolToObject(object, "wireguard_enabled", enabled) &&
         cJSON_AddNumberToObject(object, "wireguard_port", port) &&
         cJSON_AddStringToObject(object, "wireguard_endpoint", endpoint.c_str()) &&
         cJSON_AddStringToObject(object, "wireguard_address", address.c_str()) &&
         cJSON_AddStringToObject(object, "wireguard_netmask", netmask.c_str()) &&
         cJSON_AddStringToObject(object, "wireguard_allowed_ips", allowed_ips.c_str()) &&
         cJSON_AddStringToObject(object, "wireguard_peer_public_key", peer_public_key.c_str());
}

bool Settings::save(const char *json, std::optional<uint32_t> expected_epoch) {
  return save_usb(json, expected_epoch) == UsbSaveResult::Saved;
}

Settings::UsbSaveResult Settings::save_usb(const char *json, std::optional<uint32_t> expected_epoch,
                                          StockSettingsRequest request, StockSettings (*read_stock)(StockSettingsRequest)) {
  using Result = UsbSaveResult;
  const auto document = parse(json);
  if (!cJSON_IsObject(document.get()))
    return Result::Rejected;
  const auto provided = [&](const char *field) { return cJSON_HasObjectItem(document.get(), field); };
  const bool new_key = provided("home_assistant_key");
  const bool new_wifi = provided("ssid") || provided("password");
  if ((request.any() && !read_stock) || (request.wifi && new_wifi) || (request.pairing && new_key) ||
      (request.immich && (provided("immich_url") || provided("immich_key"))))
    return Result::Rejected;
  const std::lock_guard lock(storage_mutex);
  Settings saved;
  bool may_create = false;
  const auto previous = load_document(&may_create);
  const bool configured = parse_all(previous.get(), saved);
  const auto epoch = configured ? saved.reset_epoch : 0;
  if (expected_epoch && *expected_epoch != epoch)
    return Result::Rejected;
  const auto *web_password = cJSON_GetObjectItemCaseSensitive(document.get(), "web_password");
  if (!request.any() && document->child == web_password && cJSON_IsString(web_password) && !web_password->next &&
      !web_password->valuestring[0]) {
    // A password-only USB request clears the credential while retaining the saved document.
    if (!configured)
      return Result::Rejected;
    cJSON_DeleteItemFromObjectCaseSensitive(previous.get(), "web_password");
    return write_document(previous.get()) ? Result::Saved : Result::Rejected;
  }
  // A reset or unreadable native configuration blocks reimport. Existing
  // native connections and explicit pairing choices take precedence.
  const bool blocked = (!configured && !may_create) || epoch != 0;
  if (request.wifi && (blocked || configured))
    return Result::WifiUnavailable;
  if (request.immich && (blocked || saved.has_immich()))
    return Result::ImmichUnavailable;
  if (request.pairing && (blocked || saved.home_assistant_key_set))
    return Result::PairingUnavailable;
  auto stock = request.any() ? read_stock(request) : StockSettings{};
  if (request.wifi) {
    if (!stock.wifi || !stock.wifi->configured())
      return Result::WifiUnavailable;
    if (!put_wifi(document.get(), stock.wifi->ssid.data(), stock.wifi->password.data()))
      return Result::Rejected;
  } else if (!new_wifi) {
    if (!configured || !put_wifi(document.get(), saved.ssid.c_str(), saved.password.c_str()))
      return Result::Rejected;
  }
  if (request.immich) {
    if (stock.immich_key.empty() || !valid_immich_key(stock.immich_key) || !normalize_http_base(stock.immich_url))
      return Result::ImmichUnavailable;
    if (!cJSON_AddStringToObject(document.get(), "immich_url", stock.immich_url.c_str()) ||
        !cJSON_AddStringToObject(document.get(), "immich_key", stock.immich_key.c_str()))
      return Result::Rejected;
  }
  const auto *name = stock.frame_name ? &*stock.frame_name : nullptr;
  if (configured && cJSON_HasObjectItem(previous.get(), "frame_name"))
    name = &saved.frame_name;
  if (!provided("frame_name") && name && !cJSON_AddStringToObject(document.get(), "frame_name", name->c_str()))
    return Result::Rejected;
  if (configured && !provided("web_password") && !saved.web_password.empty() &&
      !cJSON_AddStringToObject(document.get(), "web_password", saved.web_password.c_str()))
    return Result::Rejected;
  if (!new_key && saved.home_assistant_key_set &&
      !cJSON_AddStringToObject(document.get(), "home_assistant_key", saved.home_assistant_key.c_str()))
    return Result::Rejected;
  if ((configured || may_create) && !fill_setup_preferences(document.get(), configured ? previous.get() : nullptr))
    return Result::Rejected;
  if (!provided("display_mode")) {
    // Established libraries without an explicit choice used the old Fit default.
    const bool keep_framing = configured && (saved.has_immich() || cJSON_HasObjectItem(previous.get(), "display_mode"));
    const auto framing = keep_framing ? saved.display_mode : DisplayMode::Fill;
    if (!cJSON_AddStringToObject(document.get(), "display_mode", display_mode_name(framing)))
      return Result::Rejected;
  }
  if (configured && !provided("conn_timeout") && saved.connection_timeout_seconds != default_connection_timeout &&
      !cJSON_AddStringToObject(document.get(), "conn_timeout", interval_label(saved.connection_timeout_seconds).c_str()))
    return Result::Rejected;
  if (configured && !provided("device_language") && saved.metadata.language != 0 &&
      !cJSON_AddStringToObject(document.get(), "device_language", espframe_i18n_catalogue::LANGUAGES[saved.metadata.language]))
    return Result::Rejected;
  for (size_t i = 0; i < ntp_fields.size(); ++i) {
    const auto *field = ntp_fields[i];
    if (configured && !provided(field) && cJSON_HasObjectItem(previous.get(), field) &&
        !cJSON_AddStringToObject(document.get(), field, saved.clock.ntp_servers[i].c_str()))
      return Result::Rejected;
  }
  Settings candidate;
  if (configured)
    candidate.stock_update_preferences = std::move(saved.stock_update_preferences);
  if (configured && !provided("wireguard") && !add_saved_wireguard(document.get(), saved.wireguard))
    return Result::Rejected;
  if (!put_epoch(document.get(), epoch) || !parse_all(document.get(), candidate) || !candidate.has_immich())
    return Result::Rejected;
  cJSON_DeleteItemFromObjectCaseSensitive(document.get(), "firmware_updates");
  if (!candidate.add_stock_update_preferences(document.get()))
    return Result::Rejected;
  // An installer that omits the service fields keeps the connection for the same library.
  if (configured && !provided("prepared_slide_url") && !provided("prepared_slide_token") && saved.has_prepared_slides() &&
      candidate.immich_url == saved.immich_url && candidate.immich_key == saved.immich_key &&
      (!cJSON_AddStringToObject(document.get(), "prepared_slide_url", saved.prepared_slide_url.c_str()) ||
       !cJSON_AddStringToObject(document.get(), "prepared_slide_token", saved.prepared_slide_token.c_str())))
    return Result::Rejected;
  if (request.pairing) {
    if (!stock.pairing || !valid_home_assistant_key(*stock.pairing))
      return Result::PairingUnavailable;
    if (!cJSON_AddStringToObject(document.get(), "home_assistant_key", encode_home_assistant_key(*stock.pairing).c_str()))
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
        !cJSON_AddStringToObject(document.get(), "immich_key", "") ||
        !fill_setup_preferences(document.get()))
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

bool Settings::save_connection_timeout(uint32_t seconds, std::string_view paired_key) {
  if (!valid_connection_timeout(seconds) || paired_key.empty())
    return false;
  const std::lock_guard lock(storage_mutex);
  Settings settings;
  if (!load(settings) || settings.home_assistant_key != paired_key)
    return false;
  if (settings.connection_timeout_seconds != seconds) {
    settings.connection_timeout_seconds = seconds;
    if (store_settings(settings) != UpdateResult::Saved)
      return false;
  }
  active_connection_timeout.store(seconds, std::memory_order_relaxed);
  return true;
}

bool Settings::save_device_language(uint8_t language, std::string_view paired_key) {
  if (language >= espframe_i18n_catalogue::LANGUAGE_COUNT || paired_key.empty())
    return false;
  const std::lock_guard lock(storage_mutex);
  Settings settings;
  if (!load(settings) || settings.home_assistant_key != paired_key)
    return false;
  if (settings.metadata.language == language && active_device_language.load(std::memory_order_relaxed) == language)
    return true;
  if (settings.metadata.language != language) {
    settings.metadata.language = language;
    if (store_settings(settings) != UpdateResult::Saved)
      return false;
  }
  publish_photo_feed(std::move(settings), false);
  return true;
}

bool Settings::save_photo_preset(PhotoPreset preset, std::string_view paired_key) {
  const std::lock_guard lock(storage_mutex);
  Settings settings;
  if (paired_key.empty() || !load(settings) || settings.home_assistant_key != paired_key || !settings.has_immich())
    return false;
  if (settings.photo_source.preset() == preset)
    return true;

  apply_photo_preset(settings, preset);
  if (store_settings(settings) != UpdateResult::Saved)
    return false;
  publish_photo_feed(std::move(settings), true);
  return true;
}

std::optional<PhotoFeedSettings> Settings::take_live_photo_feed(uint32_t revision) {
  const std::lock_guard lock(storage_mutex);
  if (revision != photo_source_revision.load(std::memory_order_relaxed))
    return {};
  return std::exchange(published_feed, std::nullopt);
}

bool Settings::expire_warm_tone_override(std::string_view timezone, const PhotoFeedSettings &active_feed) {
  const std::lock_guard lock(storage_mutex);
  Settings settings;
  if (!load(settings) || settings.clock.timezone != timezone || !settings.tone.night_override)
    return false;
  settings.tone.night_override = false;
  if (store_settings(settings) != UpdateResult::Saved)
    return false;
  // Keep the current selection or its queued live replacement. Stored
  // settings may already contain changes waiting for a restart.
  auto feed = published_feed ? std::move(*published_feed) : PhotoFeedSettings{active_feed};
  feed.tone.night_override = false;
  publish_photo_feed(std::move(feed), false);
  return true;
}

bool Settings::add_playback_fields(cJSON *object, PhotoFilterUpdate filter_update) const {
  for (size_t i = 0; i < ntp_fields.size(); ++i) {
    if (!cJSON_AddStringToObject(object, ntp_fields[i], clock.ntp_servers[i].c_str()))
      return false;
  }
  const bool filters_added = filter_update == PhotoFilterUpdate::Compose
                                 ? add_photo_filter_fields(object, shared_photo_filters(*this), true)
                                 : add_photo_filter_fields(object, photo_filters, false);
  const auto *source_name = filter_update == PhotoFilterUpdate::Compose ? photo_preset_name(photo_source.preset())
                                                                       : photo_source_name(photo_source.mode);
  return filters_added &&
         cJSON_AddStringToObject(object, "frame_name", frame_name.c_str()) &&
         cJSON_AddStringToObject(object, "immich_url", immich_url.c_str()) &&
         cJSON_AddStringToObject(object, "prepared_slide_url", prepared_slide_url.c_str()) &&
         cJSON_AddNumberToObject(object, "slide_seconds", slide_seconds) &&
         cJSON_AddBoolToObject(object, "slideshow_progress_ring_enabled", slideshow_progress_ring_enabled) &&
         cJSON_AddStringToObject(object, "conn_timeout", interval_label(connection_timeout_seconds).c_str()) &&
         cJSON_AddStringToObject(object, "transition", transition.c_str()) &&
         cJSON_AddStringToObject(object, "transition_speed", transition_speed.c_str()) &&
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
         cJSON_AddBoolToObject(object, "warm_tone_override", tone.night_override) &&
         cJSON_AddBoolToObject(object, "portrait_pairing", portrait_pairing) &&
         cJSON_AddBoolToObject(object, "portrait_pairs_only", portrait_pairs_only) &&
         cJSON_AddStringToObject(object, "portrait_pairing_range", portrait_pairing_ranges[portrait_pairing_range_index].data()) &&
         cJSON_AddStringToObject(object, "portrait_pair_framing", pair_framing_name(pair_framing)) &&
         cJSON_AddStringToObject(object, "portrait_pair_divider", pair_divider_name(pair_divider)) &&
         cJSON_AddStringToObject(object, "display_mode", display_mode_name(display_mode)) &&
         cJSON_AddStringToObject(object, "photo_orientation", photo_orientation_name(photo_orientation)) &&
         cJSON_AddStringToObject(object, "screen_rotation", screen_rotation_name(screen_rotation)) &&
         cJSON_AddBoolToObject(object, "developer_features_enabled", developer_features_enabled) &&
         cJSON_AddStringToObject(object, "photo_source", source_name) &&
         (!photo_source.legacy_preset ||
          cJSON_AddStringToObject(object, "legacy_photo_preset", photo_preset_name(photo_source.preset()))) &&
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
         cJSON_AddStringToObject(object, "device_language", espframe_i18n_catalogue::LANGUAGES[metadata.language]) &&
         cJSON_AddBoolToObject(object, "show_clock", clock.show) &&
         cJSON_AddStringToObject(object, "clock_format", clock.format.c_str()) &&
         cJSON_AddStringToObject(object, "timezone", clock.timezone.c_str());
}

bool Settings::add_stock_update_preferences(cJSON *object) const {
  const auto &preferences = stock_update_preferences;
  if (!preferences.auto_update && !preferences.frequency && !preferences.wifi_auto_update)
    return true;
  auto *fields = cJSON_AddObjectToObject(object, "firmware_updates");
  return fields &&
         (!preferences.auto_update || cJSON_AddBoolToObject(fields, "auto_update", *preferences.auto_update)) &&
         (!preferences.frequency || cJSON_AddStringToObject(fields, "update_frequency", preferences.frequency->c_str())) &&
         (!preferences.wifi_auto_update || cJSON_AddBoolToObject(fields, "wifi_auto_update", *preferences.wifi_auto_update));
}

bool Settings::save_playback(const char *json, bool *restart_required) {
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
  const auto result = update_playback(update.get(), expected_epoch);
  if (restart_required)
    *restart_required = result == UpdateResult::Saved;
  return result == UpdateResult::Saved || result == UpdateResult::SavedLive || result == UpdateResult::Unchanged;
}

void Settings::publish_live_presentation() const {
  active_slideshow_interval.store(slide_seconds, std::memory_order_relaxed);
  active_slideshow_progress_ring.store(slideshow_progress_ring_enabled, std::memory_order_relaxed);
  requested_screen_rotation.store(screen_rotation, std::memory_order_relaxed);
  active_screen_brightness.store({brightness_day, night_brightness()}, std::memory_order_relaxed);
  active_screen_schedule.store(schedule.packed(), std::memory_order_relaxed);
  active_clock_twelve_hour.store(clock.twelve_hour(), std::memory_order_relaxed);
  active_clock_visible.store(clock.show, std::memory_order_relaxed);
  requested_clock_zone.store(find_clock_zone(clock.timezone), std::memory_order_relaxed);
  publish_ntp_servers(clock.ntp_servers);
  active_device_language.store(metadata.language, std::memory_order_relaxed);
  active_warm_tone_override.store(tone.night_override, std::memory_order_relaxed);
  active_connection_timeout.store(connection_timeout_seconds, std::memory_order_relaxed);
  active_playback_transition.store(PlaybackTransition::from_settings(transition, transition_speed),
                                   std::memory_order_relaxed);
}

Settings::UpdateResult Settings::update_playback(const cJSON *update, std::optional<uint32_t> expected_epoch,
                                                 PhotoFilterUpdate filter_update, std::optional<PhotoPreset> preset) {
  std::string replacement_key;
  std::string replacement_token;
  const std::lock_guard lock(storage_mutex);
  Settings settings;
  if (!cJSON_IsObject(update) || !load(settings))
    return UpdateResult::Failed;
  if (expected_epoch && *expected_epoch != settings.reset_epoch)
    return UpdateResult::Stale;
  if (!update->child && !preset)
    return UpdateResult::Unchanged;
  const bool shared_filters = filter_update == PhotoFilterUpdate::Compose;
  const bool change_preset = preset && *preset != settings.photo_source.preset();
  const bool compose_filters = shared_filters && !change_preset && settings.photo_source.mode != PhotoSourceMode::Memories;
  std::optional<ImmichFilterConfig> stored_criteria;
  // Compare the submitted fields with the same implied filter values the
  // shared API returns. Only save those values when the filter really changes.
  if (compose_filters && (settings.photo_source.mode == PhotoSourceMode::Album ||
                         settings.photo_source.mode == PhotoSourceMode::Favorites)) {
    const cJSON *field;
    cJSON_ArrayForEach(field, update) {
      if (!is_composable_filter(field->string))
        continue;
      auto filters = shared_photo_filters(settings);
      stored_criteria = std::exchange(settings.photo_filters.criteria, std::move(filters.criteria));
      break;
    }
  }
  Json previous(cJSON_CreateObject(), cJSON_Delete);
  Json next(cJSON_CreateObject(), cJSON_Delete);
  if (!previous || !next || !settings.add_playback_fields(previous.get()))
    return UpdateResult::Failed;
  const bool was_configured = settings.has_immich();
  const auto previous_updates = settings.stock_update_preferences;
  const auto previous_night_brightness = settings.brightness_night;
  const PhotoConnectionSettings previous_connection = settings;
  const auto previous_labels = settings.photo_filters.labels;
  const auto previous_rotation = settings.screen_rotation;
  if (change_preset) {
    if (*preset == PhotoPreset::Memories || *preset == PhotoPreset::Custom)
      settings.photo_filters = shared_photo_filters(settings);
    apply_photo_preset(settings, *preset);
  }
  if (!read_field(update, "immich_url", settings.immich_url, true) ||
      !read_field(update, "immich_key", replacement_key, true) ||
      !read_field(update, "frame_name", settings.frame_name, true) ||
      !read_field(update, "prepared_slide_url", settings.prepared_slide_url, true) ||
      !read_field(update, "prepared_slide_token", replacement_token, true) || !read_playback(update, settings) ||
      !read_stock_update_preferences(update, settings.stock_update_preferences))
    return UpdateResult::Invalid;
  if (cJSON_HasObjectItem(update, "screen_rotation") && portrait_screen(settings.screen_rotation) &&
      !settings.developer_features_enabled)
    return UpdateResult::Invalid;
  if (!replacement_key.empty())
    settings.immich_key = replacement_key;
  if (settings.prepared_slide_url.empty()) {
    if (!replacement_token.empty())
      return UpdateResult::Invalid;
    settings.prepared_slide_token.clear();
  } else {
    if (!normalize_http_base(settings.prepared_slide_url) ||
        (settings.prepared_slide_url != previous_connection.prepared_slide_url && replacement_token.empty()))
      return UpdateResult::Invalid;
    if (!replacement_token.empty())
      settings.prepared_slide_token = std::move(replacement_token);
  }
  if (!normalize(settings))
    return UpdateResult::Invalid;
  const bool connection_changed = previous_connection != static_cast<const PhotoConnectionSettings &>(settings);
  const bool rotation_changed = previous_rotation != settings.screen_rotation;
  if (!settings.add_playback_fields(next.get()))
    return UpdateResult::Failed;
  const bool fixed_night_brightness = previous_night_brightness != settings.brightness_night;
  // Playback clients can send a full snapshot. Classify the actual changes
  // after validation, including an explicitly pinned night brightness.
  bool restart = !was_configured || !settings.has_immich();
  bool changed = connection_changed || fixed_night_brightness ||
                 settings.stock_update_preferences != previous_updates;
  bool selection_changed = connection_changed;
  bool photo_changed = connection_changed;
  bool live_presentation_changed = false;
  bool filters_changed = false;
  const cJSON *field;
  cJSON_ArrayForEach(field, next.get()) {
    const std::string_view key(field->string);
    if (key == "immich_url" || key == "prepared_slide_url" || key == "legacy_photo_preset" ||
        std::ranges::find(filter_label_fields, key) != filter_label_fields.end() ||
        cJSON_Compare(field, cJSON_GetObjectItemCaseSensitive(previous.get(), field->string), true))
      continue;
    changed = true;
    if (!change_preset && changes_photo_source(key))
      settings.photo_source.legacy_preset.reset();
    if (shared_filters && is_composable_filter(key)) {
      if (!compose_filters)
        continue;
      filters_changed = true;
    }
    const bool selects_photos = changes_photo_selection(key);
    const bool presents_photos = changes_photo_presentation(key);
    const bool presents_playback = changes_live_presentation(key);
    const bool rotation_disables_pairing = rotation_changed && portrait_screen(settings.screen_rotation) &&
                                          key == "portrait_pairing";
    selection_changed |= selects_photos && !rotation_disables_pairing;
    photo_changed |= selects_photos || presents_photos;
    live_presentation_changed |= presents_playback;
    restart |= !selects_photos && !presents_photos && !presents_playback && key != "developer_features_enabled";
  }
  if (cJSON_HasObjectItem(update, "c6_auto_update")) {
    bool enabled{};
    if (!read_flag(update, "c6_auto_update", enabled))
      return UpdateResult::Invalid;
    changed |= settings.stock_update_preferences.wifi_auto_update != enabled;
    settings.stock_update_preferences.wifi_auto_update = enabled;
  }
  const auto previous_wireguard = settings.wireguard;
  if (!read_wireguard_fields(update, settings.wireguard))
    return UpdateResult::Invalid;
  changed |= settings.wireguard != previous_wireguard;
  if (!changed && !change_preset && previous_labels == settings.photo_filters.labels)
    return UpdateResult::Unchanged;
  if (filters_changed) {
    const auto &criteria = settings.photo_filters.criteria;
    const bool any_enabled = std::ranges::any_of(filter_flags, [&](const auto &flag) {
      return criteria.*flag.second;
    });
    settings.photo_source.mode = any_enabled || settings.photo_source.mode == PhotoSourceMode::Custom
                                     ? PhotoSourceMode::Custom : PhotoSourceMode::Library;
  } else if (stored_criteria) {
    settings.photo_filters.criteria = std::move(*stored_criteria);
  }
  previous.reset();
  next.reset();
  const auto result = store_settings(settings);
  if (result == UpdateResult::Saved && !restart) {
    if (live_presentation_changed)
      settings.publish_live_presentation();
    if (connection_changed)
      settings.pending_connection = std::move(static_cast<PhotoConnectionSettings &>(settings));
    if (photo_changed || change_preset)
      publish_photo_feed(std::move(settings), selection_changed || change_preset);
    return UpdateResult::SavedLive;
  }
  return result;
}

bool Settings::reset(ResetMode mode) {
  const std::lock_guard lock(storage_mutex);
  Settings saved;
  if (!load(saved))
    return false;
  Settings reset;
  if (mode == ResetMode::Customization) {
    reset.ssid = std::move(saved.ssid);
    reset.password = std::move(saved.password);
    reset.web_password = std::move(saved.web_password);
    reset.home_assistant_key = std::move(saved.home_assistant_key);
    reset.home_assistant_key_set = saved.home_assistant_key_set;
    reset.stock_update_preferences = std::move(saved.stock_update_preferences);
  } else {
    const auto defaults = parse(new_setup_preferences);
    if (!defaults || !read_playback(defaults.get(), reset))
      return false;
  }
  reset.reset_epoch = saved.reset_epoch == std::numeric_limits<uint32_t>::max() ? 1 : saved.reset_epoch + 1;
  return store_settings(reset, mode == ResetMode::Factory) == UpdateResult::Saved;
}

}  // namespace espframe::native
