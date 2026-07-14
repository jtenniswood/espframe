from __future__ import annotations

import json
from pathlib import Path

from asset_generation.paths import ROOT
from product_config import load_contract_manifest, load_product


CONFIGURATION_CONTRACT_HEADER_PATH = ROOT / "components" / "espframe" / "configuration_contract_generated.h"
WRITABLE_ENTITY_DOMAINS = {"number", "select", "switch", "text"}


def _entity_parts(raw: object) -> tuple[str, str] | None:
    if isinstance(raw, dict):
        domain = str(raw.get("domain", "")).strip()
        name = str(raw.get("name", "")).strip()
    else:
        text = str(raw or "")
        domain, separator, name = text.partition("/")
        if not separator:
            return None
    if domain not in WRITABLE_ENTITY_DOMAINS or not name:
        return None
    return domain, name


def configuration_fields(product: dict[str, object] | None = None) -> list[dict[str, object]]:
    data = product if product is not None else load_product()
    fields: list[dict[str, object]] = []
    seen: set[str] = set()

    def add(key: str, raw_entity: object, secret: bool = False) -> None:
        parts = _entity_parts(raw_entity)
        if not key or parts is None or key in seen:
            return
        seen.add(key)
        fields.append({"key": key, "domain": parts[0], "name": parts[1], "secret": secret})

    for setting in data["settings"]:
        add(str(setting.get("key", "")).strip(), setting.get("entity"))

    project = data["project"]
    for key, spec in project.get("web_static_entities", {}).items():
        if isinstance(spec, dict):
            add(str(key).strip(), spec.get("entity"))
    for key, spec in project.get("web_manual_entities", {}).items():
        if isinstance(spec, dict):
            add(str(key).strip(), spec.get("entity"), secret=str(key).strip() == "api_key")
    return fields


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
        "configuration_available": True,
        "configuration_read": True,
        "configuration_write": True,
        "configuration_encoding": "application/x-www-form-urlencoded",
        "configuration_parameter": "configuration",
        "legacy_entity_api": True,
        "backup_versions": compatibility["backup_versions"],
        "setting_count": len(product["settings"]),
    }


def configuration_contract_header() -> str:
    capabilities = configuration_capabilities()
    fields = configuration_fields()
    capabilities_json = json.dumps(capabilities, separators=(",", ":"), ensure_ascii=True)
    field_rows = "\n".join(
        f'    {{"{field["key"]}", "{field["domain"]}", "{field["name"]}", {str(field["secret"]).lower()}}},'
        for field in fields
    )
    return f'''// ESPFRAME: generated from product/contract; run `npm run generate` to update.
#pragma once

#include <cstddef>

namespace esphome::espframe::contract {{

struct ConfigurationField {{
  const char *key;
  const char *domain;
  const char *entity_name;
  bool secret;
}};

inline constexpr unsigned int CONTRACT_VERSION = {capabilities["contract_version"]};
inline constexpr unsigned int API_VERSION = {capabilities["api_version"]};
inline constexpr unsigned int SETTING_COUNT = {capabilities["setting_count"]};
inline constexpr unsigned int CONFIGURATION_FIELD_COUNT = {len(fields)};
inline constexpr const char CAPABILITIES_PATH[] = "{capabilities["capabilities_path"]}";
inline constexpr const char CONFIGURATION_PATH[] = "{capabilities["configuration_path"]}";
inline constexpr const char CAPABILITIES_JSON[] = R"ESPFRAME_JSON({capabilities_json})ESPFRAME_JSON";
inline constexpr ConfigurationField CONFIGURATION_FIELDS[] = {{
{field_rows}
}};

}}  // namespace esphome::espframe::contract
'''


def generated_configuration_api_files() -> dict[Path, str]:
    return {CONFIGURATION_CONTRACT_HEADER_PATH: configuration_contract_header()}
