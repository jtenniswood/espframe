"""Validate web UI product metadata."""

from __future__ import annotations

import re
from pathlib import Path

from product_contract.common import (
    ROOT,
    TIME_YAML,
    WEB_APP,
    WEB_TEMPLATE,
    check_web_entity_default_type,
    extract_js_json_var,
    read,
    rel,
    require_contains,
    valid_entity_string,
)
from product_config import (
    backup_schema,
    default_public_manifest_urls,
    public_base_url,
    web_entity_aliases,
    web_entity_aliases_metadata,
    web_initial_fetch_keys,
    web_initial_fetch_first_keys,
    web_live_render_state_keys,
    web_live_render_state_prefixes,
    web_local_state_keys,
    web_manual_entities,
    web_manual_entities_metadata,
    web_manual_state_keys,
    web_settings_metadata,
    web_static_entities,
    web_static_entities_metadata,
    web_ui_cards_metadata,
)


WEB_STATE_REF_RE = re.compile(r"\bS\.([A-Za-z_$][A-Za-z0-9_$]*)")
WEB_ENDPOINT_REF_RE = re.compile(r"\bendpoints\.([A-Za-z_$][A-Za-z0-9_$]*)")
WEB_PRODUCT_HELPER_REF_RE = re.compile(
    r"\b(?:productSettingOptions|productNumberMin|productNumberMax|productNumberStep)\(\s*\"([^\"]+)\""
)
WEB_PRODUCT_SETTINGS_REF_RE = re.compile(r"\bPRODUCT_SETTINGS\.([A-Za-z_$][A-Za-z0-9_$]*)")


def require_web_card_render_call(web_text: str, function_name: str, errors: list[str]) -> None:
    if re.search(rf"(?<!function )\b{re.escape(function_name)}\s*\(\s*\)", web_text):
        return
    errors.append(f"Missing generated web card render call {function_name}")


def check_web_entity_metadata(product: dict, errors: list[str]) -> None:
    product_keys = {str(setting.get("key", "")).strip() for setting in product["settings"]}
    product_key_domains = {
        str(setting.get("key", "")).strip(): str(setting.get("entity", {}).get("domain", "")).strip()
        for setting in product["settings"]
    }
    product_entities = {
        f'{setting.get("entity", {}).get("domain", "")}/{setting.get("entity", {}).get("name", "")}'
        for setting in product["settings"]
    }
    static_entities = web_static_entities(product)
    initial_fetch_first_keys = web_initial_fetch_first_keys(product)
    live_render_state_keys = web_live_render_state_keys(product)
    live_render_state_prefixes = web_live_render_state_prefixes(product)
    static_entities_seen: set[str] = set()
    state_domains = dict(product_key_domains)
    local_state_keys = web_local_state_keys(product)

    if not static_entities:
        errors.append("project.web_static_entities must be a non-empty object")
    if not initial_fetch_first_keys:
        errors.append("project.web_initial_fetch_first_keys must be a non-empty list")
    if not live_render_state_keys:
        errors.append("project.web_live_render_state_keys must be a non-empty list")
    if not live_render_state_prefixes:
        errors.append("project.web_live_render_state_prefixes must be a non-empty list")
    for key in initial_fetch_first_keys:
        metadata = static_entities.get(key)
        if not metadata:
            errors.append(f"project.web_initial_fetch_first_keys {key} must point at a static web entity")
        elif metadata.get("fetch") is not True:
            errors.append(f"project.web_initial_fetch_first_keys {key} must point at a fetch-enabled static web entity")
    initial_fetch_keys = web_initial_fetch_keys(product["settings"])
    if len(initial_fetch_keys) != len(set(initial_fetch_keys)):
        errors.append("Generated web initial fetch keys must not contain duplicates")
    known_fetch_keys = product_keys | set(static_entities)
    for key in initial_fetch_keys:
        if key not in known_fetch_keys:
            errors.append(f"Generated web initial fetch key {key} must point at a product or static web entity")
    for key in product_keys:
        if key not in initial_fetch_keys:
            errors.append(f"Product setting {key} must be included in generated web initial fetch keys")
    for key, metadata in static_entities.items():
        if metadata.get("fetch") is True and key not in initial_fetch_keys:
            errors.append(f"Fetch-enabled static web entity {key} must be included in generated web initial fetch keys")
    valid_state_keys = product_keys | set(static_entities)
    for key in live_render_state_keys:
        if key not in valid_state_keys:
            errors.append(f"project.web_live_render_state_keys {key} must point at a product or static web entity")
    for prefix in live_render_state_prefixes:
        if not any(key.startswith(prefix) for key in valid_state_keys):
            errors.append(f"project.web_live_render_state_prefixes {prefix} must match at least one product or static web entity")

    for key, metadata in static_entities.items():
        if not isinstance(key, str) or not key.strip():
            errors.append("Static web entity keys must be non-empty strings")
        if key in product_keys:
            errors.append(f"Static web entity {key} duplicates a product setting key")
        if not isinstance(metadata, dict):
            errors.append(f"Static web entity {key} metadata must be an object")
            continue
        entity = metadata.get("entity")
        if not valid_entity_string(entity):
            errors.append(f"Static web entity {key} has invalid entity {entity!r}")
        elif entity in static_entities_seen:
            errors.append(f"Duplicate static web entity: {entity}")
        else:
            static_entities_seen.add(str(entity))
            domain = str(entity).split("/", 1)[0]
            state_domains[str(key)] = domain
            check_web_entity_default_type(metadata, domain, f"Static web entity {key}", errors)
            if domain == "switch" and metadata.get("boolFromState") is not True:
                errors.append(f"Static web entity {key} switch entity must set boolFromState: true")
            if domain != "switch" and metadata.get("boolFromState") is True:
                errors.append(f"Static web entity {key} boolFromState is only valid for switch entities")
            if domain == "number" and metadata.get("number") is not True:
                errors.append(f"Static web entity {key} number entity must set number: true")
            if domain != "number" and metadata.get("number") is True:
                errors.append(f"Static web entity {key} number is only valid for number entities")
        if entity in product_entities:
            errors.append(f"Static web entity {key} duplicates product entity {entity}")
        for field in ("fetch", "boolFromState", "number"):
            if field in metadata and not isinstance(metadata[field], bool):
                errors.append(f"Static web entity {key} {field} must be true or false")
        if "optionsKey" in metadata:
            options_key = str(metadata["optionsKey"]).strip()
            if not options_key:
                errors.append(f"Static web entity {key} optionsKey must be non-empty")
            elif options_key not in local_state_keys:
                errors.append(f"Static web entity {key} optionsKey must point at a project.web_local_state_keys entry")
        firmware_file = str(metadata.get("firmware_file", "")).strip()
        if not firmware_file:
            errors.append(f"Static web entity {key} is missing firmware_file")
        elif Path(firmware_file).is_absolute() or ".." in Path(firmware_file).parts:
            errors.append(f"Static web entity {key} has unsafe firmware_file path: {firmware_file}")
        elif valid_entity_string(entity):
            domain, name = str(entity).split("/", 1)
            text = read(ROOT / firmware_file, errors)
            require_contains(text, f"{domain}:", firmware_file, errors)
            require_contains(text, f"name: \"{name}\"", firmware_file, errors)

    alias_entities_seen: set[str] = set()
    entity_aliases = web_entity_aliases(product)
    if not entity_aliases:
        errors.append("project.web_entity_aliases must be a non-empty object")
    for key, aliases in entity_aliases.items():
        if key not in valid_state_keys:
            errors.append(f"Web entity alias {key} does not point at a known state key")
        if not isinstance(aliases, list) or not aliases:
            errors.append(f"Web entity alias {key} must define at least one alias")
            continue
        for alias in aliases:
            if not isinstance(alias, dict):
                errors.append(f"Web entity alias {key} entries must be objects")
                continue
            entity = alias.get("entity")
            if not valid_entity_string(entity):
                errors.append(f"Web entity alias {key} has invalid entity {entity!r}")
            else:
                alias_domain = str(entity).split("/", 1)[0]
                expected_domain = state_domains.get(key)
                check_web_entity_default_type(alias, alias_domain, f"Web entity alias {key}", errors)
                if expected_domain and alias_domain != expected_domain:
                    errors.append(
                        f"Web entity alias {key} domain {alias_domain!r} must match canonical domain {expected_domain!r}"
                    )
                if alias_domain == "switch" and alias.get("boolFromState") is not True:
                    errors.append(f"Web entity alias {key} switch alias must set boolFromState: true")
                if alias_domain != "switch" and alias.get("boolFromState") is True:
                    errors.append(f"Web entity alias {key} boolFromState is only valid for switch aliases")
                if alias_domain == "number" and alias.get("number") is not True:
                    errors.append(f"Web entity alias {key} number alias must set number: true")
                if alias_domain != "number" and alias.get("number") is True:
                    errors.append(f"Web entity alias {key} number is only valid for number aliases")
                if entity in alias_entities_seen:
                    errors.append(f"Duplicate web entity alias: {entity}")
                else:
                    alias_entities_seen.add(str(entity))
            if entity in product_entities or entity in static_entities_seen:
                errors.append(f"Web entity alias {key} duplicates canonical entity {entity}")
            for field in ("boolFromState", "number"):
                if field in alias and not isinstance(alias[field], bool):
                    errors.append(f"Web entity alias {key} {field} must be true or false")
            if "optionsKey" in alias:
                options_key = str(alias["optionsKey"]).strip()
                if not options_key:
                    errors.append(f"Web entity alias {key} optionsKey must be non-empty")
                elif options_key not in local_state_keys:
                    errors.append(f"Web entity alias {key} optionsKey must point at a project.web_local_state_keys entry")


def check_manual_web_entity_metadata(product: dict, errors: list[str]) -> None:
    product_keys = {str(setting.get("key", "")).strip() for setting in product["settings"]}
    product_entities = {
        f'{setting.get("entity", {}).get("domain", "")}/{setting.get("entity", {}).get("name", "")}'
        for setting in product["settings"]
    }
    static_entities = web_static_entities(product)
    static_entity_values = {
        str(metadata.get("entity", "")).strip()
        for metadata in static_entities.values()
        if valid_entity_string(metadata.get("entity"))
    }
    alias_entity_values = {
        str(alias.get("entity", "")).strip()
        for aliases in web_entity_aliases(product).values()
        for alias in aliases
        if valid_entity_string(alias.get("entity"))
    }
    manual_entities = web_manual_entities(product)
    local_state_keys = web_local_state_keys(product)
    manual_state_keys = web_manual_state_keys(product)
    seen_entities: set[str] = set()
    if not manual_entities:
        errors.append("project.web_manual_entities must be a non-empty object")
    if not manual_state_keys:
        errors.append("project.web_manual_state_keys must be a non-empty list")
    for key in manual_state_keys:
        if key not in manual_entities:
            errors.append(f"project.web_manual_state_keys {key} must point at a manual web entity")
        if key not in local_state_keys:
            errors.append(f"project.web_manual_state_keys {key} must also be listed in project.web_local_state_keys")

    for key, metadata in manual_entities.items():
        if not isinstance(key, str) or not key.strip():
            errors.append("Manual web entity keys must be non-empty strings")
        if key in product_keys:
            errors.append(f"Manual web entity {key} duplicates a product setting key")
        if key in static_entities:
            errors.append(f"Manual web entity {key} duplicates a static web entity key")
        if not isinstance(metadata, dict):
            errors.append(f"Manual web entity {key} metadata must be an object")
            continue
        entity = metadata.get("entity")
        if not valid_entity_string(entity):
            errors.append(f"Manual web entity {key} has invalid entity {entity!r}")
            continue
        if entity in seen_entities:
            errors.append(f"Duplicate manual web entity: {entity}")
        if entity in product_entities:
            errors.append(f"Manual web entity {key} duplicates product entity {entity}")
        if entity in static_entity_values:
            errors.append(f"Manual web entity {key} duplicates static web entity {entity}")
        if entity in alias_entity_values:
            errors.append(f"Manual web entity {key} duplicates web entity alias {entity}")
        seen_entities.add(str(entity))
        domain, name = str(entity).split("/", 1)
        firmware_file = str(metadata.get("firmware_file", "")).strip()
        if not firmware_file:
            errors.append(f"Manual web entity {key} is missing firmware_file")
            continue
        if Path(firmware_file).is_absolute() or ".." in Path(firmware_file).parts:
            errors.append(f"Manual web entity {key} has unsafe firmware_file path: {firmware_file}")
            continue
        text = read(ROOT / firmware_file, errors)
        require_contains(text, f"{domain}:", firmware_file, errors)
        require_contains(text, f"name: \"{name}\"", firmware_file, errors)


def check_generated_web_metadata(product: dict, web_text: str, errors: list[str]) -> None:
    product_settings = extract_js_json_var(web_text, "PRODUCT_SETTINGS", errors)
    if product_settings is not None and product_settings != web_settings_metadata(product["settings"]):
        errors.append("Generated web PRODUCT_SETTINGS does not match product/espframe.json")

    static_entities = extract_js_json_var(web_text, "STATIC_ENTITIES", errors)
    if static_entities is not None and static_entities != web_static_entities_metadata(product):
        errors.append("Generated web STATIC_ENTITIES does not match product/espframe.json")

    manual_entities = extract_js_json_var(web_text, "MANUAL_ENTITIES", errors)
    if manual_entities is not None and manual_entities != web_manual_entities_metadata(product):
        errors.append("Generated web MANUAL_ENTITIES does not match product/espframe.json")

    manual_state_keys = extract_js_json_var(web_text, "MANUAL_STATE_KEYS", errors)
    if manual_state_keys is not None and manual_state_keys != web_manual_state_keys(product):
        errors.append("Generated web MANUAL_STATE_KEYS does not match product/espframe.json")

    entity_aliases = extract_js_json_var(web_text, "ENTITY_ALIASES", errors)
    if entity_aliases is not None and entity_aliases != web_entity_aliases_metadata(product):
        errors.append("Generated web ENTITY_ALIASES does not match product/espframe.json")

    generated_backup_schema = extract_js_json_var(web_text, "BACKUP_SCHEMA", errors)
    if generated_backup_schema is not None and generated_backup_schema != backup_schema(product):
        errors.append("Generated web BACKUP_SCHEMA does not match product/espframe.json")

    initial_fetch_keys = extract_js_json_var(web_text, "INITIAL_FETCH_KEYS", errors)
    if initial_fetch_keys is not None and initial_fetch_keys != web_initial_fetch_keys(product["settings"]):
        errors.append("Generated web INITIAL_FETCH_KEYS does not match product/espframe.json")

    live_render_state_keys = extract_js_json_var(web_text, "LIVE_RENDER_STATE_KEYS", errors)
    if live_render_state_keys is not None and live_render_state_keys != web_live_render_state_keys(product):
        errors.append("Generated web LIVE_RENDER_STATE_KEYS does not match product/espframe.json")

    live_render_state_prefixes = extract_js_json_var(web_text, "LIVE_RENDER_STATE_PREFIXES", errors)
    if live_render_state_prefixes is not None and live_render_state_prefixes != web_live_render_state_prefixes(product):
        errors.append("Generated web LIVE_RENDER_STATE_PREFIXES does not match product/espframe.json")

    firmware_manifest_urls = extract_js_json_var(web_text, "FIRMWARE_MANIFEST_URLS", errors)
    if firmware_manifest_urls is not None and firmware_manifest_urls != default_public_manifest_urls(product):
        errors.append("Generated web FIRMWARE_MANIFEST_URLS does not match product/espframe.json")

    docs_base_url = extract_js_json_var(web_text, "DOCS_BASE_URL", errors)
    if docs_base_url is not None and docs_base_url != public_base_url(product):
        errors.append("Generated web DOCS_BASE_URL does not match product/espframe.json")

    web_ui_tabs = extract_js_json_var(web_text, "WEB_UI_TABS", errors)
    if web_ui_tabs is not None and web_ui_tabs != product["project"].get("web_ui_tabs"):
        errors.append("Generated web WEB_UI_TABS does not match product/espframe.json")

    web_ui_cards = extract_js_json_var(web_text, "WEB_UI_CARDS", errors)
    if web_ui_cards is not None and web_ui_cards != web_ui_cards_metadata(product):
        errors.append("Generated web WEB_UI_CARDS does not match product/espframe.json")

    logs_retained_lines = extract_js_json_var(web_text, "WEB_UI_LOGS_RETAINED_LINES", errors)
    if logs_retained_lines is not None and logs_retained_lines != product["project"].get("web_ui_logs_retained_lines"):
        errors.append("Generated web WEB_UI_LOGS_RETAINED_LINES does not match product/espframe.json")

    support_url = extract_js_json_var(web_text, "SUPPORT_URL", errors)
    if support_url is not None and support_url != product["project"].get("support_url"):
        errors.append("Generated web SUPPORT_URL does not match product/espframe.json")

    support_button_image_url = extract_js_json_var(web_text, "SUPPORT_BUTTON_IMAGE_URL", errors)
    if support_button_image_url is not None and support_button_image_url != product["project"].get("support_button_image_url"):
        errors.append("Generated web SUPPORT_BUTTON_IMAGE_URL does not match product/espframe.json")


def check_static_web_defaults_against_firmware(product: dict, errors: list[str]) -> None:
    text = read(TIME_YAML, errors)
    static_entities = web_static_entities(product)
    timezone_default = static_entities.get("timezone", {}).get("default")
    if not isinstance(timezone_default, str) or not timezone_default:
        errors.append("Static web entity timezone default must match the firmware initial_option")
    else:
        require_contains(
            text,
            f'initial_option: "{timezone_default}"',
            rel(TIME_YAML),
            errors,
        )

    for key in ("ntp_server_1", "ntp_server_2", "ntp_server_3"):
        default = static_entities.get(key, {}).get("default")
        if not isinstance(default, str) or not default:
            errors.append(f"Static web entity {key} default must match the firmware substitution")
            continue
        require_contains(text, f'  {key}: "{default}"', rel(TIME_YAML), errors)
        require_contains(text, f'initial_value: "${{{key}}}"', rel(TIME_YAML), errors)

    show_clock_default = static_entities.get("show_clock", {}).get("default")
    if not isinstance(show_clock_default, bool):
        errors.append("Static web entity show_clock default must be true or false")
        return
    restore_mode = "RESTORE_DEFAULT_ON" if show_clock_default is True else "RESTORE_DEFAULT_OFF"
    require_contains(text, f"restore_mode: {restore_mode}", rel(TIME_YAML), errors)


def check_web_ui_metadata(product: dict, web_template: str, web_text: str, errors: list[str]) -> None:
    project = product["project"]
    tabs = project.get("web_ui_tabs", [])
    event_source = str(project.get("web_ui_logs_event_source", "")).strip()
    event_name = str(project.get("web_ui_logs_event_name", "")).strip()
    clear_label = str(project.get("web_ui_logs_clear_label", "")).strip()
    retained_lines = project.get("web_ui_logs_retained_lines")
    labels_and_text = ((rel(WEB_TEMPLATE), web_template), (rel(WEB_APP), web_text))

    for label, text in labels_and_text:
        require_contains(text, "WEB_UI_TABS", label, errors)
        require_contains(text, "WEB_UI_CARDS", label, errors)
        require_contains(text, "webUiTabs()", label, errors)
        require_contains(text, "switchTab(t.id)", label, errors)
        require_contains(text, 'els["tab_" + t]', label, errors)
        require_contains(text, "buildLogsPage(root)", label, errors)
        require_contains(text, "appendLog(d.msg || e.data, d.lvl)", label, errors)
        require_contains(text, "ANSI_LEVEL", label, errors)
        require_contains(text, "sp-log-error", label, errors)
        require_contains(text, "sp-log-warn", label, errors)
        require_contains(text, "sp-log-info", label, errors)
        require_contains(text, "WEB_UI_LOGS_RETAINED_LINES", label, errors)
        if event_source:
            require_contains(text, f'new EventSource("{event_source}")', label, errors)
        if event_name:
            require_contains(text, f'addEventListener("{event_name}"', label, errors)
        if clear_label:
            require_contains(text, f'textContent = "{clear_label}"', label, errors)
        if isinstance(retained_lines, int) and not isinstance(retained_lines, bool):
            require_contains(text, "childNodes.length - WEB_UI_LOGS_RETAINED_LINES", label, errors)

    if isinstance(tabs, list):
        seen_tab_ids: set[str] = set()
        for tab in tabs:
            if not isinstance(tab, dict):
                errors.append("project.web_ui_tabs entries must be objects")
                continue
            tab_id = str(tab.get("id", "")).strip()
            tab_label = str(tab.get("label", "")).strip()
            if not tab_id:
                errors.append("project.web_ui_tabs entries must include id")
            elif not re.match(r"^[A-Za-z][A-Za-z0-9_]*$", tab_id):
                errors.append(f"project.web_ui_tabs id {tab_id!r} must be a JavaScript-safe identifier")
            elif tab_id in seen_tab_ids:
                errors.append(f"project.web_ui_tabs has duplicate id {tab_id!r}")
            else:
                seen_tab_ids.add(tab_id)
            if not tab_label:
                errors.append(f"project.web_ui_tabs {tab_id or '<missing>'} must include label")
            if tab_id:
                require_contains(web_template + web_text, f"{tab_id}Page", f"web UI page for {tab_id}", errors)
            if tab_label:
                require_contains(web_text, f'"label":"{tab_label}"', f"generated web UI tab {tab_label}", errors)
    else:
        errors.append("project.web_ui_tabs must be a list")

    check_web_ui_card_metadata(product, web_template, web_text, errors)


def check_web_ui_card_metadata(product: dict, web_template: str, web_text: str, errors: list[str]) -> None:
    project = product["project"]
    cards = project.get("web_ui_cards", [])
    tab_ids = {
        str(tab.get("id", "")).strip()
        for tab in project.get("web_ui_tabs", [])
        if isinstance(tab, dict) and str(tab.get("id", "")).strip()
    }
    product_keys = {str(setting.get("key", "")).strip() for setting in product["settings"]}
    static_keys = set(web_static_entities(product))
    manual_keys = set(web_manual_entities(product))
    seen_card_ids: set[str] = set()
    settings_by_card: dict[str, str] = {}
    combined_web = web_template + "\n" + web_text

    if not isinstance(cards, list) or not cards:
        errors.append("project.web_ui_cards must be a non-empty list")
        return

    for card in cards:
        if not isinstance(card, dict):
            errors.append("project.web_ui_cards entries must be objects")
            continue
        card_id = str(card.get("id", "")).strip()
        label = str(card.get("label", "")).strip()
        tab = str(card.get("tab", "")).strip()
        function_name = str(card.get("function", "")).strip()
        settings = card.get("settings", [])
        static_entities = card.get("static_entities", [])
        manual_entities = card.get("manual_entities", [])
        card_label = f"project.web_ui_cards {card_id or '<missing>'}"

        if not card_id:
            errors.append("project.web_ui_cards entries must include id")
        elif not re.match(r"^[a-z][a-z0-9_]*$", card_id):
            errors.append(f"{card_label} id must be lowercase snake_case")
        elif card_id in seen_card_ids:
            errors.append(f"project.web_ui_cards has duplicate id {card_id!r}")
        else:
            seen_card_ids.add(card_id)

        if not label:
            errors.append(f"{card_label} must include label")
        else:
            require_contains(combined_web, label, f"web card label {label}", errors)

        if tab not in tab_ids:
            errors.append(f"{card_label} tab {tab!r} must point at project.web_ui_tabs")

        if not re.match(r"^make[A-Za-z0-9]+Card$", function_name):
            errors.append(f"{card_label} function must name a make*Card function")
        else:
            require_contains(combined_web, f"function {function_name}()", f"web card function {function_name}", errors)
            require_web_card_render_call(combined_web, function_name, errors)

        for field, known_keys in (
            ("settings", product_keys),
            ("static_entities", static_keys),
            ("manual_entities", manual_keys),
        ):
            values = card.get(field, [])
            if values is None:
                values = []
            if not isinstance(values, list):
                errors.append(f"{card_label} {field} must be a list")
                continue
            seen_values: set[str] = set()
            for raw_value in values:
                value = str(raw_value).strip()
                if not value:
                    errors.append(f"{card_label} {field} contains a blank value")
                    continue
                if value in seen_values:
                    errors.append(f"{card_label} {field} contains duplicate value {value}")
                    continue
                seen_values.add(value)
                if value not in known_keys:
                    errors.append(f"{card_label} {field} references unknown key {value}")

        if isinstance(settings, list):
            for raw_key in settings:
                key = str(raw_key).strip()
                if not key:
                    continue
                if key in settings_by_card:
                    errors.append(
                        f"Product setting {key} is assigned to multiple web UI cards: "
                        f"{settings_by_card[key]} and {card_id}"
                    )
                else:
                    settings_by_card[key] = card_id

    missing_settings = sorted(product_keys - set(settings_by_card))
    if missing_settings:
        errors.append("Product settings missing from project.web_ui_cards: " + ", ".join(missing_settings))


def check_web_template_key_references(product: dict, web_template: str, errors: list[str]) -> None:
    product_keys = {str(setting.get("key", "")).strip() for setting in product["settings"]}
    static_keys = set(web_static_entities(product))
    manual_keys = set(web_manual_entities(product))
    local_state_keys = web_local_state_keys(product)
    known_state_keys = product_keys | static_keys | local_state_keys
    known_endpoint_keys = product_keys | static_keys | manual_keys

    for key in local_state_keys:
        if key in product_keys or key in static_keys:
            errors.append(f"project.web_local_state_keys {key} duplicates generated state metadata")

    state_refs = set(WEB_STATE_REF_RE.findall(web_template))
    unused_local_state_keys = sorted(local_state_keys - state_refs)
    if unused_local_state_keys:
        errors.append(
            "project.web_local_state_keys contains keys not referenced by the web template: "
            + ", ".join(unused_local_state_keys)
        )

    for key in sorted(state_refs):
        if key not in known_state_keys:
            errors.append(f"Web template references unknown state key S.{key}")

    for key in sorted(set(WEB_ENDPOINT_REF_RE.findall(web_template))):
        if key not in known_endpoint_keys:
            errors.append(f"Web template references unknown endpoint key endpoints.{key}")

    helper_keys = set(WEB_PRODUCT_HELPER_REF_RE.findall(web_template))
    helper_keys.update(WEB_PRODUCT_SETTINGS_REF_RE.findall(web_template))
    for key in sorted(helper_keys):
        if key not in product_keys:
            errors.append(f"Web template references unknown product setting {key}")
