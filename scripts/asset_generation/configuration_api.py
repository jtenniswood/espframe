from __future__ import annotations

import json
from pathlib import Path

from asset_generation.paths import ROOT
from product_config import load_contract_manifest, load_product


CONFIGURATION_CONTRACT_HEADER_PATH = ROOT / "components" / "espframe" / "configuration_contract_generated.h"


def configuration_capabilities() -> dict[str, object]:
    manifest = load_contract_manifest()
    product = load_product()
    api = manifest["configuration_api"]
    compatibility = manifest["compatibility"]
    return {
        "contract_version": manifest["contract_version"],
        "api_version": api["version"],
        "base_path": api["base_path"],
        "capabilities_path": api["capabilities_path"],
        "configuration_path": api["configuration_path"],
        "update_mode": api["update_mode"],
        "configuration_available": False,
        "legacy_entity_api": True,
        "backup_versions": compatibility["backup_versions"],
        "setting_count": len(product["settings"]),
    }


def configuration_contract_header() -> str:
    capabilities = configuration_capabilities()
    capabilities_json = json.dumps(capabilities, separators=(",", ":"), ensure_ascii=True)
    return f'''// ESPFRAME: generated from product/contract; run `npm run generate` to update.
#pragma once

namespace esphome::espframe::contract {{

inline constexpr unsigned int CONTRACT_VERSION = {capabilities["contract_version"]};
inline constexpr unsigned int API_VERSION = {capabilities["api_version"]};
inline constexpr unsigned int SETTING_COUNT = {capabilities["setting_count"]};
inline constexpr const char CAPABILITIES_PATH[] = "{capabilities["capabilities_path"]}";
inline constexpr const char CONFIGURATION_PATH[] = "{capabilities["configuration_path"]}";
inline constexpr const char CAPABILITIES_JSON[] = R"ESPFRAME_JSON({capabilities_json})ESPFRAME_JSON";

}}  // namespace esphome::espframe::contract
'''


def generated_configuration_api_files() -> dict[Path, str]:
    return {CONFIGURATION_CONTRACT_HEADER_PATH: configuration_contract_header()}
