#include "native_configuration.h"

#include "configuration_contract_generated.h"
#include "settings.h"

#include "cJSON.h"

#include <charconv>
#include <limits>
#include <memory>

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

}  // namespace

cJSON *configuration_capabilities() {
  Json body(cJSON_CreateObject(), cJSON_Delete);
  if (!body ||
      !cJSON_AddNumberToObject(body.get(), "contract_version", contract::CONTRACT_VERSION) ||
      !cJSON_AddNumberToObject(body.get(), "api_version", contract::API_VERSION) ||
      !cJSON_AddStringToObject(body.get(), "configuration_path", contract::CONFIGURATION_PATH) ||
      !cJSON_AddStringToObject(body.get(), "reset_path", contract::RESET_PATH) ||
      !cJSON_AddStringToObject(body.get(), "update_mode", "atomic") ||
      !cJSON_AddBoolToObject(body.get(), "configuration_available", true) ||
      !cJSON_AddBoolToObject(body.get(), "configuration_read", true) ||
      !cJSON_AddBoolToObject(body.get(), "configuration_write", true) ||
      !cJSON_AddBoolToObject(body.get(), "legacy_entity_api", false))
    return nullptr;
  auto *modes = cJSON_AddArrayToObject(body.get(), "reset_modes");
  Json mode(cJSON_CreateString("customization"), cJSON_Delete);
  if (!modes || !mode || !cJSON_AddItemToArray(modes, mode.get()))
    return nullptr;
  mode.release();
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

std::optional<std::string> decode_configuration_form(std::string_view form) {
  constexpr std::string_view prefix = "configuration=";
  if (!form.starts_with(prefix) || form.find('&') != std::string_view::npos)
    return std::nullopt;
  form.remove_prefix(prefix.size());
  std::string json;
  json.reserve(form.size());
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
    if (byte == '\0' || json.size() >= Settings::max_json_size)
      return std::nullopt;
    json += byte;
  }
  return json;
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
  const cJSON *value;
  cJSON_ArrayForEach(value, values) {
    const std::string_view requested_key(value->string);
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
  return {"200 OK", nullptr, {}, count};
}

ConfigurationUpdate reset_configuration(std::string_view form) {
  if (form != "mode=customization")
    return {"400 Bad Request", "invalid_mode", {}};
  if (!Settings::reset_customization())
    return {"500 Internal Server Error", "settings_unavailable", {}};
  return {"200 OK", nullptr, {}};
}

}  // namespace espframe::native
