#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

struct cJSON;

namespace espframe::native {

struct Settings;

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
ConfigurationUpdate reset_configuration(std::string_view form);

}  // namespace espframe::native
