// ESPFRAME: generated from product/contract; run `npm run generate` to update.
#pragma once

#include <cstddef>

namespace esphome::espframe::contract {

struct ConfigurationField {
  const char *key;
  const char *domain;
  const char *entity_name;
  bool secret;
};

inline constexpr unsigned int CONTRACT_VERSION = 2;
inline constexpr unsigned int API_VERSION = 1;
inline constexpr unsigned int SETTING_COUNT = 33;
inline constexpr unsigned int CONFIGURATION_FIELD_COUNT = 47;
inline constexpr const char CAPABILITIES_PATH[] = "/espframe/api/v1/capabilities";
inline constexpr const char CONFIGURATION_PATH[] = "/espframe/api/v1/configuration";
inline constexpr const char CAPABILITIES_JSON[] = R"ESPFRAME_JSON({"contract_version":2,"api_version":1,"base_path":"/espframe/api/v1","capabilities_path":"/espframe/api/v1/capabilities","configuration_path":"/espframe/api/v1/configuration","update_mode":"atomic","configuration_available":true,"configuration_read":true,"configuration_write":false,"legacy_entity_api":true,"backup_versions":[1],"setting_count":33})ESPFRAME_JSON";
inline constexpr ConfigurationField CONFIGURATION_FIELDS[] = {
    {"photo_source", "select", "Photos: Source", false},
    {"album_order", "select", "Photos: Album Order", false},
    {"date_filter_mode", "select", "Photos: Date Filter Mode", false},
    {"relative_unit", "select", "Photos: Relative Unit", false},
    {"photo_orientation", "select", "Photos: Orientation", false},
    {"display_mode", "select", "Photos: Display Mode", false},
    {"interval", "select", "Photos: Slideshow Interval", false},
    {"conn_timeout", "select", "Screen: Connection Timeout", false},
    {"screen_rotation", "select", "Screen: Rotation", false},
    {"photo_metadata_date_format", "select", "Device: Metadata Date Format", false},
    {"photo_metadata_date_taken_format", "select", "Device: Metadata Date Taken Format", false},
    {"clock_format", "select", "Clock: Format", false},
    {"update_frequency", "select", "Firmware: Update Frequency", false},
    {"auto_update", "switch", "Firmware: Auto Update", false},
    {"firmware_manifest_url", "text", "Firmware: Manifest URL", false},
    {"date_filter_enabled", "switch", "Photos: Date Filter", false},
    {"date_from", "text", "Photos: Date From", false},
    {"date_to", "text", "Photos: Date To", false},
    {"relative_amount", "number", "Photos: Relative Amount", false},
    {"schedule_enabled", "switch", "Screen: Schedule Enabled", false},
    {"schedule_on_hour", "number", "Screen: Schedule On Hour", false},
    {"schedule_off_hour", "number", "Screen: Schedule Off Hour", false},
    {"schedule_wake_timeout", "number", "Screen: Schedule Wake Timeout", false},
    {"brightness_day", "number", "Screen: Daytime Brightness", false},
    {"brightness_night", "number", "Screen: Nighttime Brightness", false},
    {"base_tone_enabled", "switch", "Screen: Tone Adjustment", false},
    {"base_tone", "number", "Screen: Display Tone", false},
    {"warm_tones_enabled", "switch", "Screen: Night Tone Adjustment", false},
    {"warm_tone_intensity", "number", "Screen: Warm Tone Intensity", false},
    {"warm_tone_override", "switch", "Screen: Warm Tone Override", false},
    {"portrait_pairing", "switch", "Photos: Portrait Pairing", false},
    {"photo_metadata_date_enabled", "switch", "Device: Metadata Date", false},
    {"photo_metadata_location_enabled", "switch", "Device: Metadata Location", false},
    {"timezone", "select", "Clock: Timezone", false},
    {"ntp_server_1", "text", "Clock: NTP Server 1", false},
    {"ntp_server_2", "text", "Clock: NTP Server 2", false},
    {"ntp_server_3", "text", "Clock: NTP Server 3", false},
    {"album_ids", "text", "Photos: Album IDs", false},
    {"album_labels", "text", "Photos: Album Labels", false},
    {"person_ids", "text", "Photos: Person IDs", false},
    {"person_labels", "text", "Photos: Person Labels", false},
    {"tag_ids", "text", "Photos: Tag IDs", false},
    {"tag_labels", "text", "Photos: Tag Labels", false},
    {"developer_features_enabled", "switch", "Developer: Features", false},
    {"show_clock", "switch", "Clock: Show", false},
    {"immich_url", "text", "Connection: Server URL", false},
    {"api_key", "text", "Connection: API Key", true},
};

}  // namespace esphome::espframe::contract
