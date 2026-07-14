// ESPFRAME: generated from product/contract; run `npm run generate` to update.
#pragma once

namespace esphome::espframe::contract {

inline constexpr unsigned int CONTRACT_VERSION = 2;
inline constexpr unsigned int API_VERSION = 1;
inline constexpr unsigned int SETTING_COUNT = 33;
inline constexpr const char CAPABILITIES_PATH[] = "/espframe/api/v1/capabilities";
inline constexpr const char CONFIGURATION_PATH[] = "/espframe/api/v1/configuration";
inline constexpr const char CAPABILITIES_JSON[] = R"ESPFRAME_JSON({"contract_version":2,"api_version":1,"base_path":"/espframe/api/v1","capabilities_path":"/espframe/api/v1/capabilities","configuration_path":"/espframe/api/v1/configuration","update_mode":"atomic","configuration_available":false,"legacy_entity_api":true,"backup_versions":[1],"setting_count":33})ESPFRAME_JSON";

}  // namespace esphome::espframe::contract
