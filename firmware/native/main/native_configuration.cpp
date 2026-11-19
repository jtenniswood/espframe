#include "native_configuration.h"

#include "configuration_contract_generated.h"
#include "frame_identity_model.h"
#include "frame_status.h"
#include "screen_control.h"
#include "settings.h"

#include "cJSON.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <memory>
#include <utility>

namespace espframe::native {
namespace {

namespace contract = esphome::espframe::contract;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

const contract::ConfigurationField *find_field(std::string_view key) {
  for (const auto &field : contract::CONFIGURATION_FIELDS) {
    if (key == field.key)
      return &field;
  }
  return nullptr;
}

std::string interval_label(unsigned seconds) {
  if (seconds % 3600 == 0)
    return std::to_string(seconds / 3600) + (seconds == 3600 ? " hour" : " hours");
  if (seconds % 60 == 0)
    return std::to_string(seconds / 60) + (seconds == 60 ? " minute" : " minutes");
  return std::to_string(seconds) + " seconds";
}

unsigned interval_seconds(std::string_view value) {
  unsigned amount{};
  const auto end = value.data() + value.size();
  const auto result = std::from_chars(value.data(), end, amount);
  if (result.ec != std::errc{} || result.ptr == end || *result.ptr != ' ')
    return 0;
  auto unit = std::string_view(result.ptr + 1, end - result.ptr - 1);
  if (unit.ends_with('s'))
    unit.remove_suffix(1);
  const unsigned multiplier = unit == "second" ? 1 : unit == "minute" ? 60 : unit == "hour" ? 3600 : 0;
  return multiplier && amount <= 86'400 / multiplier ? amount * multiplier : 0;
}

// The native player currently models photo selection as an exclusive source.
// The common API combines sources with independent include/exclude filters.
bool passes_through(const char *key, const cJSON *playback) {
  return std::string_view(key) != "photo_source" && cJSON_HasObjectItem(playback, key);
}

bool add_copy(cJSON *object, const char *key, const cJSON *value) {
  Json copy(cJSON_Duplicate(value, false), cJSON_Delete);
  if (!copy || !cJSON_AddItemToObject(object, key, copy.get()))
    return false;
  copy.release();
  return true;
}

bool bounded_configuration(std::string_view json) {
  // The envelope and its values object need only two levels of nesting.
  // cJSON returns C strings, so reject escaped NULs before reading fields.
  unsigned depth = 0;
  bool quoted = false;
  for (size_t i = 0; i < json.size(); ++i) {
    const char character = json[i];
    if (character == '"') {
      quoted = !quoted;
    } else if (quoted && character == '\\') {
      if (json.substr(i + 1, 5) == "u0000")
        return false;
      ++i;
    } else if (!quoted && (character == '{' || character == '[')) {
      if (++depth > 2)
        return false;
    } else if (!quoted && (character == '}' || character == ']')) {
      if (!depth)
        return false;
      --depth;
    }
  }
  return true;
}

const char *screen_state_name(ScreenState state) {
  switch (state) {
    case ScreenState::Awake: return "awake";
    case ScreenState::Asleep: return "asleep";
    case ScreenState::Starting: return "starting";
  }
  return "starting";
}

const char *immich_state(const FrameStatus &status, bool network_ready) {
  if (!status.configured)
    return "not_configured";
  if (!network_ready)
    return "waiting_for_network";
  switch (status.contact) {
    case ImmichResponse::Pending: return "connecting";
    case ImmichResponse::Responding: return "responding";
    case ImmichResponse::Rejected: return "rejected";
    case ImmichResponse::Unreachable: return "unreachable";
    case ImmichResponse::RequestFailed: return "request_failed";
  }
  return "connecting";
}

const char *photo_notice_name(PhotoSourceStatus notice) {
  switch (notice) {
    case PhotoSourceStatus::None: return "none";
    case PhotoSourceStatus::WaitingForTime: return "waiting_for_time";
    case PhotoSourceStatus::MemoriesEmpty: return "memories_empty";
    case PhotoSourceStatus::MemoriesUnavailable: return "memories_unavailable";
    case PhotoSourceStatus::IncompleteFilters: return "incomplete_filters";
    case PhotoSourceStatus::UnsupportedFilters: return "unsupported_filters";
    case PhotoSourceStatus::FiltersUnavailable: return "filters_unavailable";
    case PhotoSourceStatus::NoPhotos: return "no_photos";
    case PhotoSourceStatus::WaitingForPair: return "waiting_for_pair";
  }
  return "none";
}

}  // namespace

cJSON *configuration_capabilities() {
  Json body(cJSON_CreateObject(), cJSON_Delete);
  if (!body ||
      !cJSON_AddNumberToObject(body.get(), "contract_version", contract::CONTRACT_VERSION) ||
      !cJSON_AddNumberToObject(body.get(), "api_version", contract::API_VERSION) ||
      !cJSON_AddStringToObject(body.get(), "configuration_path", contract::CONFIGURATION_PATH) ||
      !cJSON_AddStringToObject(body.get(), "status_path", frame_status_path) ||
      !cJSON_AddStringToObject(body.get(), "screen_control_path", screen_control_path) ||
      !cJSON_AddStringToObject(body.get(), "reset_path", contract::RESET_PATH) ||
      !cJSON_AddStringToObject(body.get(), "update_mode", "atomic") ||
      !cJSON_AddBoolToObject(body.get(), "configuration_available", true) ||
      !cJSON_AddBoolToObject(body.get(), "configuration_read", true) ||
      !cJSON_AddBoolToObject(body.get(), "configuration_write", true) ||
      !cJSON_AddBoolToObject(body.get(), "legacy_entity_api", false))
    return nullptr;
  auto *modes = cJSON_AddArrayToObject(body.get(), "reset_modes");
  if (!modes)
    return nullptr;
  for (const auto mode : {ResetMode::Customization, ResetMode::Factory}) {
    Json name(cJSON_CreateString(reset_mode_name(mode)), cJSON_Delete);
    if (!name || !cJSON_AddItemToArray(modes, name.get()))
      return nullptr;
    name.release();
  }
  return body.release();
}

cJSON *frame_status_snapshot(const FrameStatus &status, bool network_ready, bool restarting, bool screen_pending) {
  Json body(cJSON_CreateObject(), cJSON_Delete);
  const char *playback = !status.configured ? "setup" : status.screen == ScreenState::Asleep ? "paused"
                                           : status.showed_photo ? "showing" : "loading";
  if (!body || !cJSON_AddNumberToObject(body.get(), "api_version", contract::API_VERSION) ||
      !cJSON_AddStringToObject(body.get(), "screen", screen_state_name(status.screen)) ||
      !cJSON_AddStringToObject(body.get(), "playback", playback) ||
      !cJSON_AddStringToObject(body.get(), "source", status.configured ? photo_source_name(status.source) : "none") ||
      !cJSON_AddStringToObject(body.get(), "immich", immich_state(status, network_ready)) ||
      !cJSON_AddStringToObject(body.get(), "notice", photo_notice_name(status.notice)) ||
      !cJSON_AddBoolToObject(body.get(), "screen_pending", screen_pending) ||
      !cJSON_AddBoolToObject(body.get(), "restarting", restarting))
    return nullptr;
  return body.release();
}

cJSON *configuration_snapshot(const Settings &settings) {
  Json body(cJSON_CreateObject(), cJSON_Delete);
  Json playback(cJSON_CreateObject(), cJSON_Delete);
  if (!body || !playback || !settings.add_playback_fields(playback.get()) ||
      !cJSON_AddNumberToObject(body.get(), "api_version", contract::API_VERSION) ||
      !cJSON_AddNumberToObject(body.get(), "reset_epoch", settings.reset_epoch) ||
      !cJSON_AddBoolToObject(body.get(), "api_key_configured", settings.has_immich()))
    return nullptr;
  auto *values = cJSON_AddObjectToObject(body.get(), "values");
  auto *unavailable = cJSON_AddArrayToObject(body.get(), "unavailable");
  if (!values || !unavailable ||
      !cJSON_AddStringToObject(values, "interval", interval_label(settings.slide_seconds).c_str()) ||
      !cJSON_AddStringToObject(values, "frame_name", settings.frame_name.c_str()) ||
      !cJSON_AddStringToObject(values, "prepared_slide_url", settings.prepared_slide_url.c_str()) ||
      !cJSON_AddBoolToObject(values, "prepared_slide_token_configured", settings.has_prepared_slides()))
    return nullptr;
  for (const auto &field : contract::CONFIGURATION_FIELDS) {
    if (field.secret || std::string_view(field.key) == "interval")
      continue;
    if (passes_through(field.key, playback.get())) {
      if (!add_copy(values, field.key, cJSON_GetObjectItemCaseSensitive(playback.get(), field.key)))
        return nullptr;
    } else {
      Json key(cJSON_CreateString(field.key), cJSON_Delete);
      if (!key || !cJSON_AddItemToArray(unavailable, key.get()))
        return nullptr;
      key.release();
    }
  }
  return body.release();
}

namespace {

std::optional<std::string> decode_form(std::string_view form, std::string_view prefix, size_t max_size) {
  if (!form.starts_with(prefix) || form.find('&') != std::string_view::npos)
    return std::nullopt;
  form.remove_prefix(prefix.size());
  std::string decoded;
  decoded.reserve(std::min(form.size(), max_size));
  while (!form.empty()) {
    char byte = form.front();
    form.remove_prefix(1);
    if (byte == '%') {
      if (form.size() < 2)
        return std::nullopt;
      unsigned value{};
      const auto result = std::from_chars(form.data(), form.data() + 2, value, 16);
      if (result.ec != std::errc{} || result.ptr != form.data() + 2)
        return std::nullopt;
      byte = static_cast<char>(value);
      form.remove_prefix(2);
    } else if (byte == '+') {
      byte = ' ';
    }
    if (byte == '\0' || decoded.size() >= max_size)
      return std::nullopt;
    decoded += byte;
  }
  return decoded;
}

}  // namespace

std::optional<std::string> decode_configuration_form(std::string_view form) {
  return decode_form(form, "configuration=", Settings::max_json_size);
}

std::optional<std::string> decode_identity_form(std::string_view form) {
  const auto value = decode_form(form, "name=", 256);
  std::string normalized;
  if (!value || !esphome::espframe::normalize_frame_name(*value, normalized))
    return std::nullopt;
  return normalized;
}

ConfigurationUpdate update_configuration(const char *json) {
  if (!json || std::char_traits<char>::length(json) > Settings::max_json_size || !bounded_configuration(json))
    return {"400 Bad Request", "invalid_json", {}};
  const Json request(cJSON_ParseWithOpts(json, nullptr, true), cJSON_Delete);
  if (!cJSON_IsObject(request.get()))
    return {"400 Bad Request", "invalid_json", {}};
  const auto *version = cJSON_GetObjectItemCaseSensitive(request.get(), "api_version");
  if (version && (!cJSON_IsNumber(version) || version->valuedouble != contract::API_VERSION))
    return {"409 Conflict", "unsupported_api_version", {}};
  const auto *epoch = cJSON_GetObjectItemCaseSensitive(request.get(), "reset_epoch");
  std::optional<uint32_t> expected_epoch;
  if (epoch) {
    if (!cJSON_IsNumber(epoch) || !(epoch->valuedouble >= 0 &&
                                   epoch->valuedouble <= std::numeric_limits<uint32_t>::max()))
      return {"409 Conflict", "reset_epoch_mismatch", {}};
    const auto value = static_cast<uint32_t>(epoch->valuedouble);
    if (value != epoch->valuedouble)
      return {"409 Conflict", "reset_epoch_mismatch", {}};
    expected_epoch = value;
  }
  const auto *values = cJSON_GetObjectItemCaseSensitive(request.get(), "values");
  if (!cJSON_IsObject(values))
    return {"400 Bad Request", "missing_values", {}};

  const Json update(cJSON_CreateObject(), cJSON_Delete);
  const Json playback(cJSON_CreateObject(), cJSON_Delete);
  if (!update || !playback || !Settings{}.add_playback_fields(playback.get()))
    return {"500 Internal Server Error", "settings_unavailable", {}};

  size_t count = 0;
  std::optional<std::string> frame_name;
  const cJSON *value;
  cJSON_ArrayForEach(value, values) {
    const std::string_view requested_key(value->string);
    if (requested_key == "frame_name") {
      const auto *input = cJSON_GetStringValue(value);
      std::string name;
      if (frame_name || !input || !esphome::espframe::normalize_frame_name(input, name))
        return {"422 Unprocessable Entity", "invalid_value", value->string};
      if (!cJSON_AddStringToObject(update.get(), "frame_name", name.c_str()))
        return {"500 Internal Server Error", "settings_unavailable", {}};
      frame_name = std::move(name);
      ++count;
      continue;
    }
    if (requested_key == "prepared_slide_url" || requested_key == "prepared_slide_token") {
      if (!cJSON_IsString(value) || cJSON_HasObjectItem(update.get(), value->string))
        return {"422 Unprocessable Entity", "invalid_value", value->string};
      if (!add_copy(update.get(), value->string, value))
        return {"500 Internal Server Error", "settings_unavailable", {}};
      ++count;
      continue;
    }
    const auto *field = find_field(value->string);
    if (!field)
      return {"422 Unprocessable Entity", "unknown_field", value->string};
    const auto *key = field->key;
    if (std::string_view(key) == "interval") {
      const auto *label = cJSON_GetStringValue(value);
      const auto seconds = label ? interval_seconds(label) : 0;
      if (seconds < 2 || cJSON_HasObjectItem(update.get(), "slide_seconds"))
        return {"422 Unprocessable Entity", "invalid_value", key};
      if (!cJSON_AddNumberToObject(update.get(), "slide_seconds", seconds))
        return {"500 Internal Server Error", "settings_unavailable", {}};
    } else {
      const auto *native_key = key;
      if (std::string_view(key) == "api_key") {
        const auto *secret = cJSON_GetStringValue(value);
        if (!secret || !*secret)
          return {"422 Unprocessable Entity", "invalid_value", key};
        native_key = "immich_key";
      } else if (field->secret || !passes_through(key, playback.get())) {
        return {"422 Unprocessable Entity", "unavailable", key};
      }
      if (cJSON_HasObjectItem(update.get(), native_key))
        return {"422 Unprocessable Entity", "invalid_value", key};
      if (!add_copy(update.get(), native_key, value))
        return {"500 Internal Server Error", "settings_unavailable", {}};
    }
    ++count;
  }

  const auto result = count == 0 && !expected_epoch ? Settings::UpdateResult::Saved
                                                    : Settings::update_playback(update.get(), expected_epoch);
  if (result == Settings::UpdateResult::Invalid)
    return {"422 Unprocessable Entity", "invalid_value", {}};
  if (result == Settings::UpdateResult::Stale)
    return {"409 Conflict", "reset_epoch_mismatch", {}};
  if (result == Settings::UpdateResult::Failed)
    return {"500 Internal Server Error", "settings_unavailable", {}};
  return {"200 OK", nullptr, {}, count, std::move(frame_name)};
}

std::optional<ResetMode> decode_reset_form(std::string_view form) {
  if (form == "mode=customization")
    return ResetMode::Customization;
  if (form == "mode=factory")
    return ResetMode::Factory;
  return {};
}

const char *reset_mode_name(ResetMode mode) {
  return mode == ResetMode::Factory ? "factory" : "customization";
}

ConfigurationUpdate reset_configuration(ResetMode mode) {
  if (!Settings::reset(mode))
    return {"500 Internal Server Error", "settings_unavailable", {}};
  return {"200 OK", nullptr, {}};
}

}  // namespace espframe::native
