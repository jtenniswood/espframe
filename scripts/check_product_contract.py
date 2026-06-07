#!/usr/bin/env python3
"""Validate the shared product metadata against the checked-in project.

This is the first release gate for the reset architecture. It catches drift
between product metadata, firmware YAML, the custom web UI, docs, and CI before
we start generating larger parts of the project from the product schema.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

from product_config import (
    DOCS_SETTINGS_TABLE_COLUMNS,
    DOCS_SETTINGS_TABLES,
    WEB_ENTITY_ALIASES,
    WEB_LOCAL_STATE_KEYS,
    WEB_MANUAL_ENTITIES,
    WEB_STATIC_ENTITIES,
    default_public_manifest_urls,
    device_public_manifest_urls,
    load_product,
    public_base_url,
    public_url,
    release_matrix_devices,
    web_entity_aliases_metadata,
    web_initial_fetch_keys,
    web_manual_entities_metadata,
    web_settings_metadata,
    web_static_entities_metadata,
)


ROOT = Path(__file__).resolve().parent.parent
WEB_TEMPLATE = ROOT / "docs" / "webserver" / "src" / "app.template.js"
WEB_APP = ROOT / "docs" / "public" / "webserver" / "app.js"
TIME_YAML = ROOT / "common" / "addon" / "time.yaml"
SETTING_DOMAINS = {"number", "select", "switch", "text"}
DOCS_TABLE_ID_RE = re.compile(r"^[A-Za-z0-9_-]+$")
WEB_STATE_REF_RE = re.compile(r"\bS\.([A-Za-z_$][A-Za-z0-9_$]*)")
WEB_ENDPOINT_REF_RE = re.compile(r"\bendpoints\.([A-Za-z_$][A-Za-z0-9_$]*)")
WEB_PRODUCT_HELPER_REF_RE = re.compile(
    r"\b(?:productSettingOptions|productNumberMin|productNumberMax|productNumberStep)\(\s*\"([^\"]+)\""
)
WEB_PRODUCT_SETTINGS_REF_RE = re.compile(r"\bPRODUCT_SETTINGS\.([A-Za-z_$][A-Za-z0-9_$]*)")


def rel(path: Path) -> str:
    return str(path.relative_to(ROOT))


def read(path: Path, errors: list[str]) -> str:
    if not path.is_file():
        errors.append(f"Missing file: {rel(path)}")
        return ""
    return path.read_text()


def require_contains(text: str, needle: str, label: str, errors: list[str]) -> None:
    if needle not in text:
        errors.append(f"{label} is missing {needle!r}")


def extract_js_json_var(text: str, var_name: str, errors: list[str]) -> object | None:
    match = re.search(rf"\bvar {re.escape(var_name)} = (.*?);", text)
    if not match:
        errors.append(f"Generated web app is missing {var_name}")
        return None
    try:
        return json.loads(match.group(1))
    except json.JSONDecodeError as exc:
        errors.append(f"Generated web app {var_name} is not valid JSON: {exc}")
        return None


def valid_entity_string(value: object) -> bool:
    if not isinstance(value, str) or "/" not in value:
        return False
    domain, name = value.split("/", 1)
    return bool(domain.strip() and name.strip())


def firmware_entity_block(text: str, name: str, filename: str, errors: list[str]) -> str:
    needle = f'name: "{name}"'
    lines = text.splitlines()
    name_index = next((idx for idx, line in enumerate(lines) if needle in line), None)
    if name_index is None:
        errors.append(f"{filename} entity is missing {needle!r}")
        return ""

    start = name_index
    while start > 0 and not lines[start].startswith("  - platform:"):
        start -= 1
    if not lines[start].startswith("  - platform:"):
        errors.append(f"{filename} entity block for {name} is missing a platform header")
        return ""

    end = len(lines)
    for idx in range(start + 1, len(lines)):
        if lines[idx].startswith("  - platform:") or (lines[idx] and not lines[idx].startswith((" ", "#"))):
            end = idx
            break

    return "\n".join(lines[start:end])


def check_path_list(setting: dict, key: str, field: str, errors: list[str]) -> list[str]:
    value = setting.get(field, [])
    if not isinstance(value, list) or not value:
        errors.append(f"Setting {key} must have a non-empty {field} list")
        return []
    result: list[str] = []
    for item in value:
        path = str(item).strip()
        if not path:
            errors.append(f"Setting {key} has a blank entry in {field}")
            continue
        if Path(path).is_absolute() or ".." in Path(path).parts:
            errors.append(f"Setting {key} has unsafe {field} path: {path}")
            continue
        result.append(path)
    return result


def check_relative_path(value: object, label: str, errors: list[str]) -> str:
    path = str(value or "").strip()
    if not path:
        errors.append(f"{label} is required")
        return ""
    if Path(path).is_absolute() or ".." in Path(path).parts:
        errors.append(f"{label} has unsafe path: {path}")
        return ""
    return path


def check_setting_schema(setting: dict, errors: list[str]) -> None:
    key = str(setting.get("key", "")).strip()
    entity = setting.get("entity") or {}
    domain = str(entity.get("domain", "")).strip()
    raw_default = setting.get("default", "")
    options = setting.get("options", [])
    developer_options = setting.get("developer_options", [])

    if domain not in SETTING_DOMAINS:
        errors.append(f"Setting {key or '<missing>'} has unsupported domain: {domain or '<missing>'}")
        return

    if domain == "select":
        if not isinstance(raw_default, str):
            errors.append(f"Select setting {key} default must be a string")
        if not isinstance(options, list) or not options:
            errors.append(f"Select setting {key} must define non-empty options")
        elif any(not isinstance(option, str) or not option for option in options):
            errors.append(f"Select setting {key} options must be non-empty strings")
        elif raw_default and raw_default not in options and not str(setting.get("firmware_initial_option", "")).startswith("${"):
            errors.append(f"Select setting {key} default is not in options")

        if developer_options:
            if not isinstance(developer_options, list):
                errors.append(f"Select setting {key} developer_options must be a list")
            elif any(not isinstance(option, str) or not option for option in developer_options):
                errors.append(f"Select setting {key} developer_options must be non-empty strings")
            elif set(developer_options).intersection(options):
                errors.append(f"Select setting {key} developer_options must not duplicate normal options")
    elif domain == "number":
        for field in ("default", "min", "max", "step"):
            if not isinstance(setting.get(field), (int, float)) or isinstance(setting.get(field), bool):
                errors.append(f"Number setting {key} needs numeric {field}")
                return
        minimum = setting["min"]
        maximum = setting["max"]
        default = setting["default"]
        step = setting["step"]
        if minimum > maximum:
            errors.append(f"Number setting {key} min must not exceed max")
        if not minimum <= default <= maximum:
            errors.append(f"Number setting {key} default must be within min/max")
        if step <= 0:
            errors.append(f"Number setting {key} step must be greater than zero")
        if options or developer_options:
            errors.append(f"Number setting {key} must not define options")
    elif domain == "switch":
        if not isinstance(raw_default, bool):
            errors.append(f"Switch setting {key} default must be true or false")
        if options or developer_options:
            errors.append(f"Switch setting {key} must not define options")
    elif domain == "text":
        if not isinstance(raw_default, str):
            errors.append(f"Text setting {key} default must be a string")
        if options or developer_options:
            errors.append(f"Text setting {key} must not define options")


def check_devices(product: dict, errors: list[str]) -> None:
    seen: set[str] = set()
    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        if not slug:
            errors.append("A product device is missing slug")
            continue
        if slug in seen:
            errors.append(f"Duplicate product device slug: {slug}")
        seen.add(slug)

        for field in (
            "name",
            "esphome_name",
            "friendly_name",
            "chip",
            "build_yaml",
            "package_yaml",
            "local_yaml",
            "device_yaml",
            "panel_url",
            "stand_url",
            "public_manifest",
            "public_beta_manifest",
        ):
            if not str(device.get(field, "")).strip():
                errors.append(f"Device {slug} is missing {field}")

        for field in ("build_yaml", "package_yaml", "local_yaml", "device_yaml"):
            path = check_relative_path(device.get(field), f"Device {slug} {field}", errors)
            if path:
                read(ROOT / path, errors)

        for field in ("panel_url", "stand_url"):
            url = str(device.get(field, "")).strip()
            if url and not url.startswith("https://"):
                errors.append(f"Device {slug} {field} must be an https URL")


def check_project_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    for field in (
        "name",
        "site_description",
        "ai_description",
        "social_image",
        "social_image_alt",
        "usb_flashing_image",
        "usb_flashing_image_alt",
        "web_installer_required_api",
        "favicon",
        "npm_package_name",
        "license_id",
        "license_name",
        "owner_name",
        "owner_url",
        "package_name",
        "repository_url",
        "release_url_base",
        "public_base_url",
        "support_url",
        "support_button_image_url",
        "node_version",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")

    package_name = str(project.get("package_name", "")).strip()
    if package_name and not re.match(r"^[A-Za-z0-9_.-]+\.[A-Za-z0-9_.-]+$", package_name):
        errors.append("project.package_name must look like a reverse-DNS package name")

    release_url_base = str(project.get("release_url_base", "")).strip()
    repository_url = str(project.get("repository_url", "")).strip().rstrip("/")
    if repository_url and not repository_url.startswith("https://github.com/"):
        errors.append("project.repository_url must be an https GitHub URL")
    if release_url_base and not release_url_base.startswith("https://"):
        errors.append("project.release_url_base must be an https URL")
    if release_url_base and not release_url_base.endswith("/"):
        errors.append("project.release_url_base must end with /")
    if repository_url and release_url_base and release_url_base != f"{repository_url}/releases/tag/":
        errors.append("project.release_url_base must be based on project.repository_url")

    for field in ("support_url", "support_button_image_url"):
        value = str(project.get(field, "")).strip()
        if value and not value.startswith("https://"):
            errors.append(f"project.{field} must be an https URL")
    owner_url = str(project.get("owner_url", "")).strip()
    if owner_url and not owner_url.startswith("https://"):
        errors.append("project.owner_url must be an https URL")
    for field in ("social_image", "usb_flashing_image", "favicon"):
        value = str(project.get(field, "")).strip()
        if value and (value.startswith("/") or ".." in Path(value).parts):
            errors.append(f"project.{field} must be a relative public asset path")
        if value:
            public_asset = ROOT / "docs" / "public" / value
            if not public_asset.is_file():
                errors.append(f"Missing file: {rel(public_asset)}")
    for field in ("web_installer_required_browsers", "web_installer_unsupported_browsers"):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")

    firmware_update = read(ROOT / "common" / "addon" / "firmware_update.yaml", errors)
    if package_name:
        require_contains(firmware_update, f"name: {package_name}", "common/addon/firmware_update.yaml", errors)


def check_npm_package_metadata(product: dict, errors: list[str]) -> None:
    expected_name = str(product["project"].get("npm_package_name", "")).strip()
    expected_license = str(product["project"].get("license_id", "")).strip()
    if not expected_name:
        errors.append("project.npm_package_name is required")
        return

    try:
        package_json = json.loads(read(ROOT / "package.json", errors) or "{}")
        package_lock = json.loads(read(ROOT / "package-lock.json", errors) or "{}")
    except json.JSONDecodeError as exc:
        errors.append(f"Package metadata JSON is invalid: {exc}")
        return

    if package_json.get("name") != expected_name:
        errors.append("package.json name must match project.npm_package_name")
    if expected_license and package_json.get("license") != expected_license:
        errors.append("package.json license must match project.license_id")
    if package_lock.get("name") != expected_name:
        errors.append("package-lock.json name must match project.npm_package_name")
    root_package = package_lock.get("packages", {}).get("", {})
    if root_package.get("name") != expected_name:
        errors.append("package-lock.json root package name must match project.npm_package_name")
    if expected_license and root_package.get("license") != expected_license:
        errors.append("package-lock.json root package license must match project.license_id")


def check_license_metadata(product: dict, errors: list[str]) -> None:
    license_id = str(product["project"].get("license_id", "")).strip()
    license_name = str(product["project"].get("license_name", "")).strip()
    if not license_id:
        errors.append("project.license_id is required")
    if not license_name:
        errors.append("project.license_name is required")

    license_text = read(ROOT / "LICENSE", errors)
    readme = read(ROOT / "README.md", errors)
    license_docs = read(ROOT / "docs" / "license.md", errors)
    if license_name:
        for label, text in (("LICENSE", license_text), ("README.md", readme), ("docs/license.md", license_docs)):
            require_contains(text, license_name, label, errors)
    if license_id:
        require_contains(read(ROOT / "package.json", errors), f'"license": "{license_id}"', "package.json", errors)


def check_public_manifest_urls(product: dict, errors: list[str]) -> None:
    base_url = public_base_url(product)
    if not base_url.startswith("https://"):
        errors.append("project.public_base_url must be an https URL")

    urls_by_slug = device_public_manifest_urls(product)
    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        urls = urls_by_slug.get(slug, {})
        for label, field in (("stable", "public_manifest"), ("beta", "public_beta_manifest")):
            path = str(device.get(field, "")).strip()
            if not path or path.startswith("/") or ".." in Path(path).parts:
                errors.append(f"Device {slug} has invalid {field}: {path}")
            url = urls.get(label, "")
            if not url.startswith(f"{base_url}/"):
                errors.append(f"Device {slug} {field} URL must be under project.public_base_url")

    default_urls = default_public_manifest_urls(product)
    firmware_update = read(ROOT / "common" / "addon" / "firmware_update.yaml", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    for label, url in default_urls.items():
        if not url.startswith("https://"):
            errors.append(f"Default {label} firmware manifest URL must be an https URL")
        for filename, text in (
            ("common/addon/firmware_update.yaml", firmware_update),
            ("docs/backup.md", backup_docs),
        ):
            require_contains(text, url, filename, errors)
    require_contains(docs_workflow, f'--base-url "{base_url}"', ".github/workflows/docs.yml", errors)

    urls_by_slug = device_public_manifest_urls(product)
    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        package_yaml = check_relative_path(device.get("package_yaml"), f"Device {slug} package_yaml", errors)
        if not package_yaml:
            continue
        package_text = read(ROOT / package_yaml, errors)
        for url in urls_by_slug.get(slug, {}).values():
            require_contains(package_text, url, package_yaml, errors)


def check_public_site_references(product: dict, errors: list[str]) -> None:
    base_url = public_base_url(product)
    docs_url = public_url("", product)
    install_url = public_url("install", product)
    web_app_url = public_url("webserver/app.js", product)
    project_name = str(product["project"].get("name", "")).strip()
    social_image_alt = str(product["project"].get("social_image_alt", "")).strip()
    usb_flashing_image = str(product["project"].get("usb_flashing_image", "")).strip()
    usb_flashing_image_alt = str(product["project"].get("usb_flashing_image_alt", "")).strip()
    web_installer_required_browsers = product["project"].get("web_installer_required_browsers", [])
    web_installer_required_api = str(product["project"].get("web_installer_required_api", "")).strip()
    web_installer_unsupported_browsers = product["project"].get("web_installer_unsupported_browsers", [])
    repository_url = str(product["project"].get("repository_url", "")).strip().rstrip("/")
    support_url = str(product["project"].get("support_url", "")).strip()
    support_button_image_url = str(product["project"].get("support_button_image_url", "")).strip()

    robots = read(ROOT / "docs" / "public" / "robots.txt", errors)
    ai_txt = read(ROOT / "docs" / "public" / "ai.txt", errors)
    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    install_docs = read(ROOT / "docs" / "install.md", errors)
    manual_setup = read(ROOT / "docs" / "manual-setup.md", errors)
    license_docs = read(ROOT / "docs" / "license.md", errors)
    roadmap = read(ROOT / "docs" / "roadmap.md", errors)
    troubleshooting_docs = read(ROOT / "docs" / "troubleshooting.md", errors)
    usb_flashing_docs = read(ROOT / "docs" / "usb-flashing.md", errors)
    release_changelog = read(ROOT / "scripts" / "release_changelog.py", errors)

    require_contains(robots, f"Sitemap: {public_url('sitemap.xml', product)}", "docs/public/robots.txt", errors)

    if project_name:
        require_contains(ai_txt, f"name: {project_name}", "docs/public/ai.txt", errors)
        require_contains(ai_txt, f'attribute to "{project_name}"', "docs/public/ai.txt", errors)
    if repository_url:
        require_contains(ai_txt, f"Source and issues: {repository_url}", "docs/public/ai.txt", errors)
        require_contains(manual_setup, f"url: {repository_url}", "docs/manual-setup.md", errors)
        require_contains(license_docs, f"({repository_url}/blob/main/LICENSE)", "docs/license.md", errors)
        require_contains(roadmap, f"({repository_url}/issues)", "docs/roadmap.md", errors)
        require_contains(release_changelog, 'project_value("repository_url"', "scripts/release_changelog.py", errors)
    require_contains(ai_txt, f"url: {docs_url}", "docs/public/ai.txt", errors)
    require_contains(ai_txt, f"Prefer canonical URLs: {docs_url} and {install_url}", "docs/public/ai.txt", errors)
    require_contains(ai_txt, f"Documentation: {docs_url}", "docs/public/ai.txt", errors)
    require_contains(readme, f"]({install_url})", "README.md installer link", errors)
    require_contains(readme, f"]({docs_url})", "README.md docs link", errors)
    require_contains(readme, base_url, "README.md public base URL", errors)
    for label, text in (("README.md", readme), ("docs/index.md", index_docs)):
        if support_url:
            require_contains(text, support_url, label, errors)
        if support_button_image_url:
            require_contains(text, support_button_image_url, label, errors)
    if social_image_alt:
        for label, text in (
            ("README.md", readme),
            ("docs/index.md", index_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            require_contains(text, f'alt="{social_image_alt}"', label, errors)
    if usb_flashing_image:
        for label, text in (
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
        ):
            require_contains(text, f'src="/{usb_flashing_image}"', label, errors)
    if usb_flashing_image_alt:
        for label, text in (
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
        ):
            require_contains(text, f'alt="{usb_flashing_image_alt}"', label, errors)
    if isinstance(web_installer_required_browsers, list):
        for label, text in (
            ("README.md", readme),
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
            ("docs/troubleshooting.md", troubleshooting_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            for browser in web_installer_required_browsers:
                if isinstance(browser, str) and browser.strip():
                    require_contains(text, browser.strip(), label, errors)
    if web_installer_required_api:
        for label, text in (
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            require_contains(text, web_installer_required_api, label, errors)
    if isinstance(web_installer_unsupported_browsers, list):
        for label, text in (
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
        ):
            for browser in web_installer_unsupported_browsers:
                if isinstance(browser, str) and browser.strip():
                    require_contains(text, browser.strip(), label, errors)

    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        esphome_name = str(device.get("esphome_name", "")).strip()
        friendly_name = str(device.get("friendly_name", "")).strip()
        model = str(device.get("model", "")).strip()
        model_code = model.split()[-1] if model else ""
        panel_url = str(device.get("panel_url", "")).strip()
        stand_url = str(device.get("stand_url", "")).strip()
        local_yaml = check_relative_path(device.get("local_yaml"), f"Device {slug} local_yaml", errors)
        package_yaml = check_relative_path(device.get("package_yaml"), f"Device {slug} package_yaml", errors)
        build_yaml = check_relative_path(device.get("build_yaml"), f"Device {slug} build_yaml", errors)
        device_yaml = check_relative_path(device.get("device_yaml"), f"Device {slug} device_yaml", errors)
        for label, text in (
            ("README.md", readme),
            ("docs/index.md", index_docs),
            ("docs/install.md", install_docs),
        ):
            if model_code:
                require_contains(text, model_code, label, errors)
            if panel_url:
                require_contains(text, panel_url, label, errors)
            if stand_url:
                require_contains(text, stand_url, label, errors)
        if esphome_name:
            for label, text in (
                ("docs/install.md", install_docs),
                ("docs/troubleshooting.md", troubleshooting_docs),
                ("docs/usb-flashing.md", usb_flashing_docs),
            ):
                require_contains(text, esphome_name, label, errors)
        if package_yaml:
            require_contains(manual_setup, f"files: [{package_yaml}]", "docs/manual-setup.md", errors)
        if build_yaml:
            require_contains(readme, f"compile /config/{build_yaml}", "README.md compile example", errors)
            build_text = read(ROOT / build_yaml, errors)
            if esphome_name:
                require_contains(build_text, f'name: "{esphome_name}"', build_yaml, errors)
            if friendly_name:
                require_contains(build_text, f'friendly_name: "{friendly_name}"', build_yaml, errors)
        if local_yaml and repository_url:
            local_text = read(ROOT / local_yaml, errors)
            if esphome_name:
                require_contains(local_text, f'name: "{esphome_name}"', local_yaml, errors)
            if friendly_name:
                require_contains(local_text, f'friendly_name: "{friendly_name}"', local_yaml, errors)
            require_contains(local_text, f"url: {repository_url}", local_yaml, errors)
        if device_yaml:
            device_text = read(ROOT / device_yaml, errors)
            require_contains(device_text, f'js_url: "{web_app_url}"', device_yaml, errors)
            if repository_url:
                require_contains(device_text, f'espframe_component_url: "{repository_url}"', device_yaml, errors)


def check_docs_site_config(product: dict, errors: list[str]) -> None:
    config = read(ROOT / "docs" / ".vitepress" / "config.mts", errors)
    project = product["project"]
    project_name = str(project.get("name", "")).strip()
    site_description = str(project.get("site_description", "")).strip()
    ai_description = str(project.get("ai_description", "")).strip()
    social_image = str(project.get("social_image", "")).strip()
    social_image_alt = str(project.get("social_image_alt", "")).strip()
    favicon = str(project.get("favicon", "")).strip()
    base_url = public_base_url(product)
    docs_url = public_url("", product)
    base_path = f"/{base_url.rstrip('/').rsplit('/', 1)[-1]}/"
    repository_url = str(project.get("repository_url", "")).strip().rstrip("/")
    owner_name = str(project.get("owner_name", "")).strip()
    owner_url = str(project.get("owner_url", "")).strip()

    if project_name:
        require_contains(config, f"title: '{project_name}'", "docs/.vitepress/config.mts", errors)
        require_contains(config, f"content: '{project_name}'", "docs/.vitepress/config.mts", errors)
        require_contains(config, f"name: '{project_name}'", "docs/.vitepress/config.mts", errors)
    if site_description:
        require_contains(config, f"description: '{site_description}'", "docs/.vitepress/config.mts", errors)
    if social_image:
        require_contains(config, f"content: `${{hostname}}{social_image}`", "docs/.vitepress/config.mts", errors)
        require_contains(config, f"image: `${{hostname}}{social_image}`", "docs/.vitepress/config.mts", errors)
    if social_image_alt:
        require_contains(config, f"content: '{social_image_alt}'", "docs/.vitepress/config.mts", errors)
    if favicon:
        require_contains(config, f"href: '{base_path}{favicon}'", "docs/.vitepress/config.mts", errors)
    if ai_description:
        require_contains(read(ROOT / "docs" / "public" / "ai.txt", errors), f"description: {ai_description}", "docs/public/ai.txt", errors)
    require_contains(config, f"const hostname = '{docs_url}'", "docs/.vitepress/config.mts", errors)
    require_contains(config, f"base: '{base_path}'", "docs/.vitepress/config.mts", errors)
    if repository_url:
        require_contains(config, f"link: '{repository_url}'", "docs/.vitepress/config.mts", errors)
        require_contains(config, f"pattern: '{repository_url}/edit/main/docs/:path'", "docs/.vitepress/config.mts", errors)
    if owner_name:
        require_contains(config, f"name: '{owner_name}'", "docs/.vitepress/config.mts", errors)
    if owner_url:
        require_contains(config, f"url: '{owner_url}'", "docs/.vitepress/config.mts", errors)


def check_device_workflow_contract(product: dict, errors: list[str]) -> None:
    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)
    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    slugs = [str(device.get("slug", "")).strip() for device in product["devices"]]
    expected_slugs = " ".join(slugs)
    for label, text in (
        (".github/workflows/release.yml", release_workflow),
        (".github/workflows/docs.yml", docs_workflow),
    ):
        require_contains(text, f"DEVICE_SLUGS: {expected_slugs}", label, errors)

    try:
        release_devices = release_matrix_devices(product)
    except RuntimeError as exc:
        errors.append(str(exc))
        return

    devices_by_slug = {str(device.get("slug", "")).strip(): device for device in product["devices"]}
    for release_device in release_devices:
        slug = release_device["slug"]
        build_yaml = str(devices_by_slug.get(slug, {}).get("build_yaml", "")).strip()
        local_yaml = str(devices_by_slug.get(slug, {}).get("local_yaml", "")).strip()
        device_dir = str(Path(local_yaml).parent) if local_yaml else ""
        for needle in (
            f"- slug: {slug}",
            f"yaml: {release_device['yaml']}",
            f"build_name: {release_device['build_name']}",
            f"chip: {release_device['chip']}",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
        require_contains(
            release_workflow,
            f"compile /config/builds/${{{{ matrix.yaml }}}}.factory.yaml",
            ".github/workflows/release.yml",
            errors,
        )
        require_contains(
            compile_workflow,
            f"compile /config/{build_yaml}",
            ".github/workflows/compile.yml",
            errors,
        )
        if device_dir:
            require_contains(
                compile_workflow,
                f'"{device_dir}/**"',
                ".github/workflows/compile.yml",
                errors,
            )
        for prefix in ("firmware", "firmware/beta"):
            require_contains(
                docs_workflow,
                f"if [ -f {prefix}/{slug}.manifest.json ]; then",
                ".github/workflows/docs.yml",
                errors,
            )
            require_contains(
                docs_workflow,
                f"cp {prefix}/{slug}.manifest.json {prefix}/manifest.json",
                ".github/workflows/docs.yml",
                errors,
            )


def check_esphome_version(product: dict, errors: list[str]) -> None:
    version = str(product["project"].get("esphome_version", "")).strip()
    if not version:
        errors.append("project.esphome_version is required")
        return

    required_refs = [
        ROOT / ".github" / "workflows" / "compile.yml",
        ROOT / ".github" / "workflows" / "release.yml",
        ROOT / "README.md",
        ROOT / "docs" / "install.md",
        ROOT / "docs" / "manual-setup.md",
    ]
    for path in required_refs:
        text = read(path, errors)
        require_contains(text, version, rel(path), errors)

    for path in (ROOT / ".github" / "workflows" / "compile.yml", ROOT / ".github" / "workflows" / "release.yml"):
        text = read(path, errors)
        require_contains(text, f"ghcr.io/esphome/esphome:{version}", rel(path), errors)


def check_workflows(errors: list[str]) -> None:
    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    require_contains(compile_workflow, '"product/**"', ".github/workflows/compile.yml", errors)

    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)
    for label, text in (
        (".github/workflows/docs.yml", docs_workflow),
        (".github/workflows/release.yml", release_workflow),
    ):
        require_contains(text, "scripts/product_config.py", label, errors)
        require_contains(text, "product/espframe.json", label, errors)


def check_node_version(product: dict, errors: list[str]) -> None:
    version = str(product["project"].get("node_version", "")).strip()
    if not version:
        errors.append("project.node_version is required")
        return
    if not re.match(r"^\d+$", version):
        errors.append("project.node_version must be a major version number")

    for path in (ROOT / ".github" / "workflows" / "compile.yml", ROOT / ".github" / "workflows" / "docs.yml"):
        text = read(path, errors)
        require_contains(text, f"node-version: {version}", rel(path), errors)


def check_web_entity_metadata(product: dict, errors: list[str]) -> None:
    product_keys = {str(setting.get("key", "")).strip() for setting in product["settings"]}
    product_entities = {
        f'{setting.get("entity", {}).get("domain", "")}/{setting.get("entity", {}).get("name", "")}'
        for setting in product["settings"]
    }
    static_entities_seen: set[str] = set()

    for key, metadata in WEB_STATIC_ENTITIES.items():
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
        if entity in product_entities:
            errors.append(f"Static web entity {key} duplicates product entity {entity}")
        for field in ("fetch", "boolFromState", "number"):
            if field in metadata and not isinstance(metadata[field], bool):
                errors.append(f"Static web entity {key} {field} must be true or false")
        if "optionsKey" in metadata and not str(metadata["optionsKey"]).strip():
            errors.append(f"Static web entity {key} optionsKey must be non-empty")

    alias_entities_seen: set[str] = set()
    valid_state_keys = product_keys | set(WEB_STATIC_ENTITIES)
    for key, aliases in WEB_ENTITY_ALIASES.items():
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
            elif entity in alias_entities_seen:
                errors.append(f"Duplicate web entity alias: {entity}")
            else:
                alias_entities_seen.add(str(entity))
            if entity in product_entities or entity in static_entities_seen:
                errors.append(f"Web entity alias {key} duplicates canonical entity {entity}")
            for field in ("boolFromState", "number"):
                if field in alias and not isinstance(alias[field], bool):
                    errors.append(f"Web entity alias {key} {field} must be true or false")
            if "optionsKey" in alias and not str(alias["optionsKey"]).strip():
                errors.append(f"Web entity alias {key} optionsKey must be non-empty")


def check_manual_web_entity_metadata(errors: list[str]) -> None:
    seen_entities: set[str] = set()
    for key, metadata in WEB_MANUAL_ENTITIES.items():
        if not isinstance(key, str) or not key.strip():
            errors.append("Manual web entity keys must be non-empty strings")
        if not isinstance(metadata, dict):
            errors.append(f"Manual web entity {key} metadata must be an object")
            continue
        entity = metadata.get("entity")
        if not valid_entity_string(entity):
            errors.append(f"Manual web entity {key} has invalid entity {entity!r}")
            continue
        if entity in seen_entities:
            errors.append(f"Duplicate manual web entity: {entity}")
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
        require_contains(text, f"name: \"{name}\"", firmware_file, errors)
        if domain == "button":
            require_contains(text, "button:", firmware_file, errors)
        elif domain == "update":
            require_contains(text, "update:", firmware_file, errors)


def check_generated_web_metadata(product: dict, web_text: str, errors: list[str]) -> None:
    product_settings = extract_js_json_var(web_text, "PRODUCT_SETTINGS", errors)
    if product_settings is not None and product_settings != web_settings_metadata(product["settings"]):
        errors.append("Generated web PRODUCT_SETTINGS does not match product/espframe.json")

    static_entities = extract_js_json_var(web_text, "STATIC_ENTITIES", errors)
    if static_entities is not None and static_entities != web_static_entities_metadata():
        errors.append("Generated web STATIC_ENTITIES does not match product_config.py")

    manual_entities = extract_js_json_var(web_text, "MANUAL_ENTITIES", errors)
    if manual_entities is not None and manual_entities != web_manual_entities_metadata():
        errors.append("Generated web MANUAL_ENTITIES does not match product_config.py")

    entity_aliases = extract_js_json_var(web_text, "ENTITY_ALIASES", errors)
    if entity_aliases is not None and entity_aliases != web_entity_aliases_metadata():
        errors.append("Generated web ENTITY_ALIASES does not match product_config.py")

    initial_fetch_keys = extract_js_json_var(web_text, "INITIAL_FETCH_KEYS", errors)
    if initial_fetch_keys is not None and initial_fetch_keys != web_initial_fetch_keys(product["settings"]):
        errors.append("Generated web INITIAL_FETCH_KEYS does not match product/espframe.json")

    firmware_manifest_urls = extract_js_json_var(web_text, "FIRMWARE_MANIFEST_URLS", errors)
    if firmware_manifest_urls is not None and firmware_manifest_urls != default_public_manifest_urls(product):
        errors.append("Generated web FIRMWARE_MANIFEST_URLS does not match product/espframe.json")

    docs_base_url = extract_js_json_var(web_text, "DOCS_BASE_URL", errors)
    if docs_base_url is not None and docs_base_url != public_base_url(product):
        errors.append("Generated web DOCS_BASE_URL does not match product/espframe.json")

    support_url = extract_js_json_var(web_text, "SUPPORT_URL", errors)
    if support_url is not None and support_url != product["project"].get("support_url"):
        errors.append("Generated web SUPPORT_URL does not match product/espframe.json")

    support_button_image_url = extract_js_json_var(web_text, "SUPPORT_BUTTON_IMAGE_URL", errors)
    if support_button_image_url is not None and support_button_image_url != product["project"].get("support_button_image_url"):
        errors.append("Generated web SUPPORT_BUTTON_IMAGE_URL does not match product/espframe.json")


def check_static_web_defaults_against_firmware(errors: list[str]) -> None:
    text = read(TIME_YAML, errors)
    timezone_default = WEB_STATIC_ENTITIES["timezone"].get("default")
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
        default = WEB_STATIC_ENTITIES[key].get("default")
        if not isinstance(default, str) or not default:
            errors.append(f"Static web entity {key} default must match the firmware substitution")
            continue
        require_contains(text, f'  {key}: "{default}"', rel(TIME_YAML), errors)
        require_contains(text, f'initial_value: "${{{key}}}"', rel(TIME_YAML), errors)

    show_clock_default = WEB_STATIC_ENTITIES["show_clock"].get("default")
    if not isinstance(show_clock_default, bool):
        errors.append("Static web entity show_clock default must be true or false")
        return
    restore_mode = "RESTORE_DEFAULT_ON" if show_clock_default is True else "RESTORE_DEFAULT_OFF"
    require_contains(text, f"restore_mode: {restore_mode}", rel(TIME_YAML), errors)


def check_web_template_key_references(product: dict, web_template: str, errors: list[str]) -> None:
    product_keys = {str(setting.get("key", "")).strip() for setting in product["settings"]}
    static_keys = set(WEB_STATIC_ENTITIES)
    manual_keys = set(WEB_MANUAL_ENTITIES)
    known_state_keys = product_keys | static_keys | set(WEB_LOCAL_STATE_KEYS)
    known_endpoint_keys = product_keys | static_keys | manual_keys

    for key in sorted(set(WEB_STATE_REF_RE.findall(web_template))):
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


def check_docs_table_membership(product: dict, errors: list[str]) -> None:
    settings_by_key = {str(setting.get("key", "")): setting for setting in product["settings"]}
    table_memberships: set[tuple[str, str]] = set()
    for path, table_blocks in DOCS_SETTINGS_TABLES.items():
        relative_path = rel(path)
        for block_id, table in table_blocks.items():
            for key in [str(item) for item in table["settings"]]:
                table_memberships.add((relative_path, key))
                setting = settings_by_key.get(key)
                if not setting:
                    errors.append(f"{relative_path} settings table {block_id} references unknown setting {key}")
                    continue
                docs_files = [str(item) for item in setting.get("docs_files", [])]
                if relative_path not in docs_files:
                    errors.append(
                        f"{relative_path} settings table {block_id} includes {key}, "
                        f"but product docs_files does not include {relative_path}"
                    )
    generated_docs_files = {rel(path) for path in DOCS_SETTINGS_TABLES}
    for key, setting in settings_by_key.items():
        for docs_file in [str(item) for item in setting.get("docs_files", [])]:
            if docs_file in generated_docs_files and (docs_file, key) not in table_memberships:
                errors.append(f"{key} declares {docs_file} but is not included in a generated settings table")


def check_docs_table_metadata(product: dict, errors: list[str]) -> None:
    settings_by_key = {str(setting.get("key", "")).strip() for setting in product["settings"]}
    seen_tables: set[tuple[str, str]] = set()
    all_table_refs: set[tuple[str, str]] = set()

    for path, table_blocks in DOCS_SETTINGS_TABLES.items():
        if not isinstance(path, Path):
            errors.append(f"Generated docs table path {path!r} must be a Path")
            relative_path = str(path)
        else:
            try:
                relative_path = rel(path)
            except ValueError:
                relative_path = str(path)
                errors.append(f"Generated docs table path {relative_path} must be inside the repository")
            else:
                if path.suffix != ".md" or not relative_path.startswith("docs/"):
                    errors.append(f"Generated docs table path {relative_path} must be a docs markdown file")
                read(path, errors)

        if not isinstance(table_blocks, dict) or not table_blocks:
            errors.append(f"{relative_path} must define at least one generated settings table")
            continue

        for block_id, table in table_blocks.items():
            if not isinstance(block_id, str) or not DOCS_TABLE_ID_RE.match(block_id):
                errors.append(f"{relative_path} has invalid settings table id {block_id!r}")
                continue
            table_key = (relative_path, block_id)
            if table_key in seen_tables:
                errors.append(f"{relative_path} defines duplicate settings table {block_id}")
            seen_tables.add(table_key)

            if not isinstance(table, dict):
                errors.append(f"{relative_path} settings table {block_id} metadata must be an object")
                continue
            columns = table.get("columns", ["Setting", "Default", "Description"])
            if not isinstance(columns, list) or not columns:
                errors.append(f"{relative_path} settings table {block_id} columns must be a non-empty list")
            else:
                seen_columns: set[str] = set()
                for column in columns:
                    if not isinstance(column, str) or column not in DOCS_SETTINGS_TABLE_COLUMNS:
                        errors.append(f"{relative_path} settings table {block_id} has unsupported column {column!r}")
                    elif column in seen_columns:
                        errors.append(f"{relative_path} settings table {block_id} includes column {column} more than once")
                    seen_columns.add(str(column))

            setting_keys = table.get("settings")
            if not isinstance(setting_keys, list) or not setting_keys:
                errors.append(f"{relative_path} settings table {block_id} settings must be a non-empty list")
                continue
            seen_setting_keys: set[str] = set()
            for raw_key in setting_keys:
                key = str(raw_key).strip()
                if not key:
                    errors.append(f"{relative_path} settings table {block_id} has a blank setting key")
                    continue
                if key in seen_setting_keys:
                    errors.append(f"{relative_path} settings table {block_id} includes {key} more than once")
                seen_setting_keys.add(key)
                if key not in settings_by_key:
                    errors.append(f"{relative_path} settings table {block_id} references unknown setting {key}")
                table_ref = (relative_path, key)
                if table_ref in all_table_refs:
                    errors.append(f"{relative_path} includes {key} in more than one generated settings table")
                all_table_refs.add(table_ref)


def check_docs_table_markers(errors: list[str]) -> None:
    marker_re = re.compile(r"<!-- ESPFRAME:SETTINGS_TABLE ([A-Za-z0-9_-]+) (START|END) -->")
    expected = {
        (rel(path), block_id)
        for path, table_blocks in DOCS_SETTINGS_TABLES.items()
        for block_id in table_blocks
    }
    seen: dict[tuple[str, str], dict[str, int]] = {}
    for docs_path in sorted(ROOT.glob("docs/**/*.md")):
        relative_path = rel(docs_path)
        text = read(docs_path, errors)
        for match in marker_re.finditer(text):
            block_id, side = match.groups()
            key = (relative_path, block_id)
            if key not in expected:
                errors.append(f"{relative_path} has unregistered settings table marker {block_id}")
            seen.setdefault(key, {"START": 0, "END": 0})[side] += 1

    for key in sorted(expected):
        counts = seen.get(key, {"START": 0, "END": 0})
        if counts["START"] != 1 or counts["END"] != 1:
            errors.append(
                f"{key[0]} settings table {key[1]} needs exactly one START and one END marker "
                f"(found {counts['START']} START, {counts['END']} END)"
            )
    for key, counts in sorted(seen.items()):
        if key in expected and (counts["START"] != 1 or counts["END"] != 1):
            continue
        if key not in expected and (counts["START"] != counts["END"] or counts["START"] > 1):
            errors.append(
                f"{key[0]} unregistered settings table {key[1]} has "
                f"{counts['START']} START and {counts['END']} END markers"
            )


def check_setting(setting: dict, web_text: str, errors: list[str]) -> None:
    key = str(setting.get("key", "")).strip()
    entity = setting.get("entity") or {}
    domain = str(entity.get("domain", "")).strip()
    name = str(entity.get("name", "")).strip()
    raw_default = setting.get("default", "")
    default = str(raw_default)
    web_default = json.dumps(raw_default, separators=(",", ":"))
    docs_default = str(setting.get("docs_default", default))
    options = [str(option) for option in setting.get("options", [])]
    developer_options = [str(option) for option in setting.get("developer_options", [])]

    if not key or not domain or not name:
        errors.append(f"Setting {key or '<missing>'} needs key, entity.domain, and entity.name")
        return
    check_setting_schema(setting, errors)

    entity_id = f"{domain}/{name}"
    require_contains(web_text, f'"{entity_id}"', f"web UI mapping for {key}", errors)
    require_contains(web_text, key, f"web UI state key for {key}", errors)
    require_contains(web_text, web_default, f"web UI default for {key}", errors)
    for option in options:
        require_contains(web_text, option, f"web UI option for {key}", errors)
    for option in developer_options:
        require_contains(web_text, option, f"web UI developer option for {key}", errors)

    firmware_files = check_path_list(setting, key, "firmware_files", errors)
    for filename in firmware_files:
        text = read(ROOT / str(filename), errors)
        block = firmware_entity_block(text, name, str(filename), errors)
        for option in options:
            require_contains(block, f'"{option}"', f"{filename} option for {key}", errors)
        for option in developer_options:
            require_contains(block, f'"{option}"', f"{filename} developer option for {key}", errors)
        if entity.get("domain") == "select":
            initial_option = str(setting.get("firmware_initial_option", raw_default))
            if initial_option.startswith("${"):
                if (
                    f"initial_option: {initial_option}" not in block
                    and f'initial_option: "{initial_option}"' not in block
                ):
                    errors.append(f"{filename} initial_option for {key} is missing {initial_option!r}")
            else:
                require_contains(block, f'initial_option: "{initial_option}"', f"{filename} initial_option for {key}", errors)
        if entity.get("domain") == "number":
            for product_field, firmware_field in (
                ("default", "initial_value"),
                ("min", "min_value"),
                ("max", "max_value"),
                ("step", "step"),
            ):
                if product_field in setting:
                    value = str(setting[product_field])
                    require_contains(block, f"{firmware_field}: {value}", f"{filename} {firmware_field} for {key}", errors)
        if entity.get("domain") == "switch" and isinstance(raw_default, bool):
            restore_mode = "RESTORE_DEFAULT_ON" if raw_default else "RESTORE_DEFAULT_OFF"
            require_contains(block, f"restore_mode: {restore_mode}", f"{filename} restore_mode for {key}", errors)

    docs_files = check_path_list(setting, key, "docs_files", errors)
    for filename in docs_files:
        text = read(ROOT / str(filename), errors)
        require_contains(text, docs_default, f"{filename} default for {key}", errors)
        for field in ("docs_label", "docs_description", "docs_format", "docs_type"):
            if setting.get(field):
                require_contains(text, str(setting[field]), f"{filename} {field} for {key}", errors)


def check_settings(product: dict, errors: list[str]) -> None:
    web_template = read(WEB_TEMPLATE, errors)
    web_text = read(WEB_APP, errors)
    check_web_entity_metadata(product, errors)
    check_manual_web_entity_metadata(errors)
    check_generated_web_metadata(product, web_text, errors)
    check_static_web_defaults_against_firmware(errors)
    check_web_template_key_references(product, web_template, errors)
    check_docs_table_metadata(product, errors)
    check_docs_table_membership(product, errors)
    check_docs_table_markers(errors)
    require_contains(web_template, "__ESPFRAME_PRODUCT_SETTINGS__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_STATIC_ENTITIES__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_MANUAL_ENTITIES__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_ENTITY_ALIASES__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_INITIAL_FETCH_KEYS__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_FIRMWARE_MANIFEST_URLS__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_DOCS_BASE_URL__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_SUPPORT_URL__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_SUPPORT_BUTTON_IMAGE_URL__", rel(WEB_TEMPLATE), errors)
    for needle in (
        "registerStaticEntityStateDefaults",
        "registerProductSettingStateDefaults",
        "registerManualEntityEndpoints",
        "registerProductSettingEndpoints",
        "registerManualStateEntities",
        "registerProductSettingEntities",
        "endpoints[key] = eid(parts.domain, parts.name);",
        "ENTITY_STATE_MAP[productSpec.entity] = stateSpec;",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    seen: set[str] = set()
    seen_entities: set[str] = set()
    for setting in product["settings"]:
        key = str(setting.get("key", "")).strip()
        if key in seen:
            errors.append(f"Duplicate product setting key: {key}")
        seen.add(key)
        entity = setting.get("entity") or {}
        entity_id = f'{entity.get("domain", "")}/{entity.get("name", "")}'
        if entity_id in seen_entities:
            errors.append(f"Duplicate product setting entity: {entity_id}")
        seen_entities.add(entity_id)
        check_setting(setting, web_text, errors)


def main() -> int:
    errors: list[str] = []
    product = load_product()
    check_project_metadata(product, errors)
    check_npm_package_metadata(product, errors)
    check_license_metadata(product, errors)
    check_devices(product, errors)
    check_public_manifest_urls(product, errors)
    check_public_site_references(product, errors)
    check_docs_site_config(product, errors)
    check_device_workflow_contract(product, errors)
    check_esphome_version(product, errors)
    check_node_version(product, errors)
    check_workflows(errors)
    check_settings(product, errors)

    if errors:
        for error in errors:
            print(f"product contract error: {error}", file=sys.stderr)
        return 1

    print("product contract validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
