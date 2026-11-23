#pragma once

#include "daylight.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

struct cJSON;

namespace espframe::native {

struct Settings;
struct FrameStatus;
enum class ResetMode;

struct ConfigurationUpdate {
  const char *status;
  const char *error;
  std::string field;
  size_t updated{};
  std::optional<std::string> frame_name{};
  bool restart_required{};
};

cJSON *configuration_capabilities();
cJSON *configuration_snapshot(const Settings &settings, std::optional<SolarTimes> solar_times = {});
cJSON *frame_status_snapshot(const FrameStatus &status, bool network_ready, bool restarting, bool screen_pending);
std::optional<std::string> decode_configuration_form(std::string_view form);
std::optional<std::string> decode_identity_form(std::string_view form);
ConfigurationUpdate update_configuration(const char *json);
std::optional<ResetMode> decode_reset_form(std::string_view form);
const char *reset_mode_name(ResetMode mode);
ConfigurationUpdate reset_configuration(ResetMode mode);

}  // namespace espframe::native
