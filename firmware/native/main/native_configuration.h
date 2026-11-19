#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

struct cJSON;

namespace espframe::native {

struct Settings;
enum class ResetMode;

struct ConfigurationUpdate {
  const char *status;
  const char *error;
  std::string field;
  size_t updated{};
};

cJSON *configuration_capabilities();
cJSON *configuration_snapshot(const Settings &settings);
std::optional<std::string> decode_configuration_form(std::string_view form);
ConfigurationUpdate update_configuration(const char *json);
std::optional<ResetMode> decode_reset_form(std::string_view form);
const char *reset_mode_name(ResetMode mode);
ConfigurationUpdate reset_configuration(ResetMode mode);

}  // namespace espframe::native
