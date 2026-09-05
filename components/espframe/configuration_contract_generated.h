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
inline constexpr unsigned int SETTING_COUNT = 48;
inline constexpr unsigned int CONFIGURATION_FIELD_COUNT = 69;
inline constexpr const char CAPABILITIES_PATH[] = "/espframe/api/v1/capabilities";
inline constexpr const char CONFIGURATION_PATH[] = "/espframe/api/v1/configuration";
inline constexpr const char CAPABILITIES_JSON[] = R"ESPFRAME_JSON({"contract_version":2,"api_version":1,"base_path":"/espframe/api/v1","capabilities_path":"/espframe/api/v1/capabilities","configuration_path":"/espframe/api/v1/configuration","update_mode":"atomic","configuration_available":true,"configuration_read":true,"configuration_write":true,"configuration_encoding":"application/x-www-form-urlencoded","configuration_parameter":"configuration","legacy_entity_api":true,"backup_versions":[1,2],"setting_count":48})ESPFRAME_JSON";
inline constexpr ConfigurationField CONFIGURATION_FIELDS[] = {
    {"photo_source", "select", "Photos: Source", false},
    {"album_order", "select", "Photos: Album Order", false},
    {"tag_matching", "select", "Photos: Tag Matching", false},
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
    {"c6_auto_update", "switch", "WiFi Firmware: Auto Update", false},
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
    {"mono_mode_enabled", "switch", "Mono Mode", false},
    {"base_tone_enabled", "switch", "Screen: Tone Adjustment", false},
    {"base_tone", "number", "Screen: Display Tone", false},
    {"warm_tones_enabled", "switch", "Screen: Night Tone Adjustment", false},
    {"warm_tone_intensity", "number", "Screen: Warm Tone Intensity", false},
    {"warm_tone_override", "switch", "Screen: Warm Tone Override", false},
    {"portrait_pairing", "switch", "Photos: Portrait Pairing", false},
    {"portrait_pairing_range", "select", "Photos: Portrait Pairing Range", false},
    {"portrait_pairs_only", "switch", "Photos: Paired Portraits Only", false},
    {"photo_metadata_date_enabled", "switch", "Device: Metadata Date", false},
    {"photo_metadata_location_enabled", "switch", "Device: Metadata Location", false},
    {"albums_enabled", "switch", "Photos: Albums Enabled", false},
    {"people_enabled", "switch", "Photos: People Enabled", false},
    {"tags_enabled", "switch", "Photos: Tags Enabled", false},
    {"inclusion_matching", "select", "Photos: Inclusion Groups", false},
    {"album_matching", "select", "Photos: Album Matching", false},
    {"person_matching", "select", "Photos: Person Matching", false},
    {"favorite_mode", "select", "Photos: Favorites", false},
    {"minimum_rating", "select", "Photos: Minimum Rating", false},
    {"filter_country", "text", "Photos: Country", false},
    {"filter_state", "text", "Photos: State or Province", false},
    {"filter_city", "text", "Photos: City", false},
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
    {"excluded_album_ids", "text", "Photos: Excluded Album IDs", false},
    {"excluded_album_labels", "text", "Photos: Excluded Album Labels", false},
    {"excluded_person_ids", "text", "Photos: Excluded Person IDs", false},
    {"excluded_person_labels", "text", "Photos: Excluded Person Labels", false},
    {"excluded_tag_ids", "text", "Photos: Excluded Tag IDs", false},
    {"excluded_tag_labels", "text", "Photos: Excluded Tag Labels", false},
    {"memories_migration_notice", "switch", "Photos: Memories Migration Notice", false},
    {"developer_features_enabled", "switch", "Developer: Features", false},
    {"show_clock", "switch", "Clock: Show", false},
    {"immich_url", "text", "Connection: Server URL", false},
    {"api_key", "text", "Connection: API Key", true},
};

}  // namespace esphome::espframe::contract
