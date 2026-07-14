from __future__ import annotations

import re
from pathlib import Path

from product_contract.common import (
    ROOT,
    WEB_TEMPLATE,
    check_relative_path,
    read,
    read_web_source,
    rel,
    require_contains,
)
from product_config import public_url


def check_generated_asset_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    outputs = [str(value).strip() for value in project.get("generated_asset_outputs", []) if str(value).strip()]
    sources = [str(value).strip() for value in project.get("generated_asset_sources", []) if str(value).strip()]
    placeholders = [str(value).strip() for value in project.get("web_template_placeholders", []) if str(value).strip()]

    generator_paths = [ROOT / "scripts" / "generate_assets.py"] + sorted((ROOT / "scripts" / "asset_generation").glob("*.py"))
    generator = "\n".join(read(path, errors) for path in generator_paths)
    generator_label = "asset generator sources"
    web_template = read_web_source(errors)
    package_json = read(ROOT / "package.json", errors)

    expected_outputs = {
        "product/espframe.json",
        "components/espframe/tz_data_generated.h",
        "common/addon/time.yaml",
        "devices/guition-esp32-p4-jc8012p4a1/packages.yaml",
        "docs/public/webserver/app.js",
        "docs/public/webserver/style.css",
    }
    expected_sources = {
        "components/espframe/timezones.py",
        "docs/webserver/src/app.template.js",
        "docs/webserver/src/app_shell.js",
        "docs/webserver/src/backup_import.js",
        "docs/webserver/src/compat.js",
        "docs/webserver/src/endpoints.js",
        "docs/webserver/src/live_helpers.js",
        "docs/webserver/src/runtime_state.js",
        "docs/webserver/src/settings_controls.js",
        "docs/webserver/src/startup_wizard.js",
        "docs/webserver/src/style.css",
        "product/contract/devices.json",
        "product/contract/manifest.json",
        "product/contract/project.json",
        "product/contract/schema.json",
        "product/contract/settings.json",
        "scripts/asset_generation/device_packages.py",
        "scripts/asset_generation/product_manifest.py",
        "scripts/product_config.py",
    }
    missing_outputs = sorted(expected_outputs - set(outputs))
    missing_sources = sorted(expected_sources - set(sources))
    overlapping_paths = sorted(set(outputs).intersection(sources))
    if missing_outputs:
        errors.append("project.generated_asset_outputs is missing paths: " + ", ".join(missing_outputs))
    if missing_sources:
        errors.append("project.generated_asset_sources is missing paths: " + ", ".join(missing_sources))
    if overlapping_paths:
        errors.append("Generated asset paths must not be listed as both sources and outputs: " + ", ".join(overlapping_paths))

    for filename in outputs + sources:
        path = check_relative_path(filename, f"Generated asset path {filename}", errors)
        if path:
            read(ROOT / path, errors)
            path_name = Path(path).name
            if path_name in {"devices.json", "manifest.json", "project.json", "schema.json", "settings.json", "product_config.py"}:
                require_contains(generator, "load_product", generator_label, errors)
            elif path_name == "product_manifest.py":
                require_contains(generator, "legacy_product_manifest", generator_label, errors)
            elif path_name in {"device_packages.py", "packages.yaml"}:
                require_contains(generator, "generated_device_package_files", generator_label, errors)
            else:
                require_contains(generator, path_name, generator_label, errors)
    template_placeholders = set(re.findall(r"__ESPFRAME_[A-Z0-9_]+__", web_template))
    configured_placeholders = set(placeholders)
    missing_placeholders = sorted(template_placeholders - configured_placeholders)
    extra_placeholders = sorted(configured_placeholders - template_placeholders)
    if missing_placeholders:
        errors.append(
            "project.web_template_placeholders is missing template placeholders: "
            + ", ".join(missing_placeholders)
        )
    if extra_placeholders:
        errors.append(
            "project.web_template_placeholders lists placeholders not used by the web template: "
            + ", ".join(extra_placeholders)
        )
    for placeholder in placeholders:
        require_contains(web_template, placeholder, rel(WEB_TEMPLATE), errors)
        require_contains(generator, placeholder, generator_label, errors)
    for needle in (
        "python3 scripts/generate_assets.py",
        "python3 scripts/generate_assets.py --check",
        "check:generated",
        "webserver:build",
    ):
        require_contains(package_json, needle, "package.json", errors)
    for needle in (
        "write_or_check",
        "generated_device_package_files",
        "replace_timezone_yaml",
        "web_app_bundle",
        "render_settings_table",
    ):
        require_contains(generator, needle, generator_label, errors)

    generated_headers = project.get("generated_asset_output_headers", {})
    if not isinstance(generated_headers, dict) or not generated_headers:
        errors.append("project.generated_asset_output_headers must be a non-empty object")
    else:
        for output, raw_header in generated_headers.items():
            if output not in outputs:
                errors.append(f"project.generated_asset_output_headers references non-generated output {output}")
                continue
            header = str(raw_header).strip()
            if not header:
                errors.append(f"project.generated_asset_output_headers is missing {output}")
                continue
            output_text = read(ROOT / output, errors)
            if output_text and not output_text.startswith(header):
                errors.append(f"{output} must start with its generated-file header")


def check_factory_firmware_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    purpose = str(project.get("factory_firmware_purpose", "")).strip()
    secret_policy = str(project.get("factory_firmware_secret_policy", "")).strip()
    network_mode = str(project.get("factory_firmware_network_mode", "")).strip()
    setup_method = str(project.get("factory_firmware_setup_method", "")).strip()
    local_use = str(project.get("factory_firmware_local_use", "")).strip()
    esphome_config_mount = str(project.get("esphome_config_mount", "")).strip()
    firmware_version_placeholder = str(project.get("firmware_version_placeholder_line", "")).strip()
    factory_css_include = str(project.get("web_server_factory_css_include", "")).strip()
    factory_js_include = str(project.get("web_server_factory_js_include", "")).strip()

    install_docs = read(ROOT / "docs" / "install.md", errors)
    connectivity_yaml = read(ROOT / "common" / "addon" / "connectivity.yaml", errors)
    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)

    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        build_yaml = check_relative_path(device.get("build_yaml"), f"Device {slug} build_yaml", errors)
        if not build_yaml:
            continue
        build_text = read(ROOT / build_yaml, errors)
        for value in (purpose, secret_policy, network_mode, setup_method, local_use):
            if value:
                require_contains(build_text, value, build_yaml, errors)
        if firmware_version_placeholder:
            require_contains(build_text, firmware_version_placeholder, build_yaml, errors)
        if factory_css_include:
            require_contains(build_text, f'css_include: "{factory_css_include}"', build_yaml, errors)
        if factory_js_include:
            require_contains(build_text, f'js_include: "{factory_js_include}"', build_yaml, errors)
        if esphome_config_mount:
            require_contains(
                compile_workflow,
                'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.factory.yaml"',
                ".github/workflows/compile.yml",
                errors,
            )
            require_contains(
                compile_workflow,
                'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.yaml"',
                ".github/workflows/compile.yml",
                errors,
            )
            require_contains(
                release_workflow,
                'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.factory.yaml"',
                ".github/workflows/release.yml",
                errors,
            )
            require_contains(
                release_workflow,
                'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.yaml"',
                ".github/workflows/release.yml",
                errors,
            )

    if network_mode:
        require_contains(install_docs, "hotspot", "docs/install.md", errors)
        require_contains(connectivity_yaml, 'ssid: "${name}"', "common/addon/connectivity.yaml", errors)
        require_contains(connectivity_yaml, "wifi:", "common/addon/connectivity.yaml", errors)
        require_contains(connectivity_yaml, "ap:", "common/addon/connectivity.yaml", errors)
    if setup_method:
        require_contains(connectivity_yaml, "captive_portal:", "common/addon/connectivity.yaml", errors)
        require_contains(install_docs, setup_method.replace("_", " "), "docs/install.md", errors)


def check_web_server_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    port = project.get("web_server_port")
    version = project.get("web_server_version")
    include_internal = project.get("web_server_include_internal")
    public_app_path = str(project.get("web_server_public_app_path", "")).strip()
    factory_js_url = str(project.get("web_server_factory_js_url", ""))
    factory_css_include = str(project.get("web_server_factory_css_include", "")).strip()
    factory_js_include = str(project.get("web_server_factory_js_include", "")).strip()
    sorting_groups = project.get("web_server_sorting_groups", [])
    group_ids = {
        str(group.get("id", "")).strip()
        for group in sorting_groups
        if isinstance(group, dict) and str(group.get("id", "")).strip()
    }

    if public_app_path and (public_app_path.startswith("/") or ".." in Path(public_app_path).parts):
        errors.append("project.web_server_public_app_path must be a relative public asset path")
    public_app_url = public_url(public_app_path, product) if public_app_path else ""

    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        device_yaml = check_relative_path(device.get("device_yaml"), f"Device {slug} device_yaml", errors)
        build_yaml = check_relative_path(device.get("build_yaml"), f"Device {slug} build_yaml", errors)
        if device_yaml:
            device_text = read(ROOT / device_yaml, errors)
            if isinstance(port, int) and not isinstance(port, bool):
                require_contains(device_text, f"  port: {port}", device_yaml, errors)
            if isinstance(version, int) and not isinstance(version, bool):
                require_contains(device_text, f"  version: {version}", device_yaml, errors)
            if isinstance(include_internal, bool):
                require_contains(device_text, f"  include_internal: {str(include_internal).lower()}", device_yaml, errors)
            if public_app_url:
                require_contains(device_text, f'  js_url: "{public_app_url}"', device_yaml, errors)
            for group in sorting_groups if isinstance(sorting_groups, list) else []:
                if not isinstance(group, dict):
                    continue
                group_id = str(group.get("id", "")).strip()
                name = str(group.get("name", "")).strip()
                weight = group.get("sorting_weight")
                if group_id:
                    require_contains(device_text, f"    - id: {group_id}", device_yaml, errors)
                if name:
                    require_contains(device_text, f'      name: "{name}"', device_yaml, errors)
                if isinstance(weight, int) and not isinstance(weight, bool):
                    require_contains(device_text, f"      sorting_weight: {weight}", device_yaml, errors)
        if build_yaml:
            build_text = read(ROOT / build_yaml, errors)
            if isinstance(port, int) and not isinstance(port, bool):
                require_contains(build_text, f"  port: {port}", build_yaml, errors)
            if isinstance(version, int) and not isinstance(version, bool):
                require_contains(build_text, f"  version: {version}", build_yaml, errors)
            require_contains(build_text, f'  js_url: "{factory_js_url}"', build_yaml, errors)
            if factory_css_include:
                require_contains(build_text, f'  css_include: "{factory_css_include}"', build_yaml, errors)
            if factory_js_include:
                require_contains(build_text, f'  js_include: "{factory_js_include}"', build_yaml, errors)

    if group_ids:
        for yaml_path in list((ROOT / "common").rglob("*.yaml")) + list((ROOT / "devices").rglob("*.yaml")):
            text = read(yaml_path, errors)
            for group_id in re.findall(r"sorting_group_id:\s*([A-Za-z0-9_-]+)", text):
                if group_id not in group_ids:
                    errors.append(f"{rel(yaml_path)} references unknown web_server sorting group {group_id}")

    for include_path in (factory_css_include, factory_js_include):
        if not include_path:
            continue
        normalized = include_path
        while normalized.startswith("../"):
            normalized = normalized[3:]
        if not (ROOT / normalized).is_file():
            errors.append(f"project web server include is missing: {normalized}")


def check_external_components_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    component_names = [
        str(value).strip() for value in project.get("external_component_names", []) if str(value).strip()
    ]
    git_source_type = str(project.get("external_component_git_source_type", "")).strip()
    local_source_type = str(project.get("external_component_local_source_type", "")).strip()
    git_path = str(project.get("external_component_git_path", "")).strip()
    local_path = str(project.get("external_component_local_path", "")).strip()
    component_ref = str(project.get("external_component_ref", "")).strip()
    components_inline = f"components: [{', '.join(component_names)}]" if component_names else ""
    repository_url = str(project.get("repository_url", "")).strip()

    for component in component_names:
        component_dir = ROOT / "components" / component
        if not component_dir.is_dir():
            errors.append(f"Missing external component directory: {rel(component_dir)}")
        init_file = component_dir / "__init__.py"
        if not init_file.is_file():
            errors.append(f"Missing external component entrypoint: {rel(init_file)}")

    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        device_yaml = check_relative_path(device.get("device_yaml"), f"Device {slug} device_yaml", errors)
        build_yaml = check_relative_path(device.get("build_yaml"), f"Device {slug} build_yaml", errors)
        if device_yaml:
            device_text = read(ROOT / device_yaml, errors)
            require_contains(device_text, "external_components:", device_yaml, errors)
            if git_source_type:
                require_contains(device_text, f"      type: {git_source_type}", device_yaml, errors)
            if repository_url:
                require_contains(device_text, f'espframe_component_url: "{repository_url}"', device_yaml, errors)
                require_contains(device_text, "      url: ${espframe_component_url}", device_yaml, errors)
            if component_ref:
                require_contains(device_text, f'espframe_component_ref: "{component_ref}"', device_yaml, errors)
                require_contains(device_text, "      ref: ${espframe_component_ref}", device_yaml, errors)
            if git_path:
                require_contains(device_text, f"      path: {git_path}", device_yaml, errors)
            if components_inline:
                require_contains(device_text, f"    {components_inline}", device_yaml, errors)
            require_contains(device_text, "    refresh: 0s", device_yaml, errors)
            require_contains(device_text, "espframe:", device_yaml, errors)
            require_contains(device_text, "  id: espframe_core", device_yaml, errors)
        if build_yaml:
            build_text = read(ROOT / build_yaml, errors)
            require_contains(build_text, "external_components:", build_yaml, errors)
            if local_source_type:
                require_contains(build_text, f"      type: {local_source_type}", build_yaml, errors)
            if local_path:
                require_contains(build_text, f"      path: {local_path}", build_yaml, errors)
            if components_inline:
                require_contains(build_text, f"    {components_inline}", build_yaml, errors)

    if local_path:
        normalized = local_path
        while normalized.startswith("../"):
            normalized = normalized[3:]
        if not (ROOT / normalized).is_dir():
            errors.append(f"project.external_component_local_path is missing: {normalized}")
    if git_path and not (ROOT / git_path).is_dir():
        errors.append(f"project.external_component_git_path is missing: {git_path}")
