"""Validate device metadata against firmware YAML assets."""

from __future__ import annotations

from product_contract.common import ROOT, check_relative_path, read, rel, require_contains


def _require_display_panel_contract(device: dict, device_yaml: str, device_yaml_label: str, errors: list[str]) -> None:
    display_panel = str(device.get("display_panel", "")).strip()
    if not display_panel:
        return

    stock_model = f"model: {display_panel}"
    if stock_model in device_yaml:
        return

    custom_panel_markers = {
        "JC8012P4A1": (
            "model: CUSTOM",
            "pixel_mode: 16bit",
            "color_depth: 16",
            "init_sequence:",
        ),
    }
    markers = custom_panel_markers.get(display_panel)
    if markers and all(marker in device_yaml for marker in markers):
        return

    errors.append(f"{device_yaml_label} is missing {stock_model!r}")


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
            "model",
            "esp32_variant",
            "flash_size",
            "framework_type",
            "platformio_flash_mode",
            "esp32_hosted_variant",
            "psram_mode",
            "psram_speed",
            "display_panel",
            "lvgl_buffer_size",
            "lvgl_byte_order",
            "lvgl_rotation_substitution",
            "touch_platform",
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

        for field in ("display_native_width", "display_native_height", "display_ui_width", "display_ui_height"):
            value = device.get(field)
            if not isinstance(value, int) or isinstance(value, bool) or value < 1:
                errors.append(f"Device {slug} {field} must be a positive integer")
        for field in ("engineering_sample", "idf_experimental_features"):
            if not isinstance(device.get(field), bool):
                errors.append(f"Device {slug} {field} must be true or false")
        if not isinstance(device.get("lvgl_resume_on_input"), bool):
            errors.append(f"Device {slug} lvgl_resume_on_input must be true or false")
        sdkconfig_options = device.get("sdkconfig_options", {})
        if not isinstance(sdkconfig_options, dict) or not sdkconfig_options:
            errors.append(f"Device {slug} sdkconfig_options must be a non-empty object")
        else:
            for option, value in sdkconfig_options.items():
                if not isinstance(option, str) or not option.strip():
                    errors.append(f"Device {slug} sdkconfig_options keys must be non-empty strings")
                if not isinstance(value, str) or not value.strip():
                    errors.append(f"Device {slug} sdkconfig_options.{option} must be a non-empty string")
        build_flags = device.get("platformio_build_flags", [])
        if not isinstance(build_flags, list) or not build_flags:
            errors.append(f"Device {slug} platformio_build_flags must be a non-empty list")
        elif any(not isinstance(flag, str) or not flag.strip() for flag in build_flags):
            errors.append(f"Device {slug} platformio_build_flags must only contain non-empty strings")
        hardware_pins = device.get("hardware_pins", {})
        if not isinstance(hardware_pins, dict) or not hardware_pins:
            errors.append(f"Device {slug} hardware_pins must be a non-empty object")
        else:
            for pin_name, pin in hardware_pins.items():
                if not isinstance(pin_name, str) or not pin_name.strip():
                    errors.append(f"Device {slug} hardware_pins keys must be non-empty strings")
                if not isinstance(pin, str) or not pin.strip():
                    errors.append(f"Device {slug} hardware_pins.{pin_name} must be a non-empty string")
        power_rail = device.get("mipi_dsi_power_rail", {})
        if not isinstance(power_rail, dict):
            errors.append(f"Device {slug} mipi_dsi_power_rail must be an object")
        else:
            for field in ("id", "voltage"):
                if not str(power_rail.get(field, "")).strip():
                    errors.append(f"Device {slug} mipi_dsi_power_rail.{field} is required")
            if not isinstance(power_rail.get("channel"), int) or isinstance(power_rail.get("channel"), bool):
                errors.append(f"Device {slug} mipi_dsi_power_rail.channel must be an integer")
        if not str(device.get("i2c_frequency", "")).strip():
            errors.append(f"Device {slug} i2c_frequency is required")
        package_includes = device.get("package_includes", [])
        if not isinstance(package_includes, list) or not package_includes:
            errors.append(f"Device {slug} package_includes must be a non-empty list")
        else:
            seen_includes: set[str] = set()
            for include in package_includes:
                if not isinstance(include, dict):
                    errors.append(f"Device {slug} package_includes entries must be objects")
                    continue
                alias = str(include.get("alias", "")).strip()
                path = str(include.get("path", "")).strip()
                if not alias:
                    errors.append(f"Device {slug} package_includes entry is missing alias")
                if not path:
                    errors.append(f"Device {slug} package_includes entry is missing path")
                if alias in seen_includes:
                    errors.append(f"Device {slug} package_includes has duplicate alias {alias}")
                seen_includes.add(alias)
        package_substitutions = device.get("package_substitutions", {})
        if not isinstance(package_substitutions, dict) or not package_substitutions:
            errors.append(f"Device {slug} package_substitutions must be a non-empty object")
        else:
            for name, value in package_substitutions.items():
                if not isinstance(name, str) or not name.strip():
                    errors.append(f"Device {slug} package_substitutions keys must be non-empty strings")
                if not isinstance(value, str) or not value.strip():
                    errors.append(f"Device {slug} package_substitutions.{name} must be a non-empty string")
        font_assets = device.get("font_assets", [])
        if not isinstance(font_assets, list) or not font_assets:
            errors.append(f"Device {slug} font_assets must be a non-empty list")
        else:
            for font in font_assets:
                if not isinstance(font, dict):
                    errors.append(f"Device {slug} font_assets entries must be objects")
                    continue
                for field in ("id", "file"):
                    if not str(font.get(field, "")).strip():
                        errors.append(f"Device {slug} font_assets entry is missing {field}")
                for field in ("size", "bpp"):
                    if not isinstance(font.get(field), int) or isinstance(font.get(field), bool) or font.get(field) < 1:
                        errors.append(f"Device {slug} font_assets.{font.get('id', '<unknown>')}.{field} must be a positive integer")
        location_font_extra_files = device.get("location_font_extra_files", [])
        if not isinstance(location_font_extra_files, list) or not location_font_extra_files:
            errors.append(f"Device {slug} location_font_extra_files must be a non-empty list")
        elif any(not isinstance(font_file, str) or not font_file.strip() for font_file in location_font_extra_files):
            errors.append(f"Device {slug} location_font_extra_files must only contain non-empty strings")
        icon_font = device.get("icon_font", {})
        if not isinstance(icon_font, dict):
            errors.append(f"Device {slug} icon_font must be an object")
        else:
            for field in ("id", "file"):
                if not str(icon_font.get(field, "")).strip():
                    errors.append(f"Device {slug} icon_font.{field} is required")
            for field in ("size", "bpp"):
                if not isinstance(icon_font.get(field), int) or isinstance(icon_font.get(field), bool) or icon_font.get(field) < 1:
                    errors.append(f"Device {slug} icon_font.{field} must be a positive integer")
        icon_substitutions = device.get("icon_substitutions", {})
        if not isinstance(icon_substitutions, dict) or not icon_substitutions:
            errors.append(f"Device {slug} icon_substitutions must be a non-empty object")
        else:
            for name, glyph in icon_substitutions.items():
                if not isinstance(name, str) or not name.strip():
                    errors.append(f"Device {slug} icon_substitutions keys must be non-empty strings")
                if not isinstance(glyph, str) or not glyph.strip():
                    errors.append(f"Device {slug} icon_substitutions.{name} must be a non-empty string")

        for field in ("panel_url", "stand_url"):
            url = str(device.get(field, "")).strip()
            if url and not url.startswith("https://"):
                errors.append(f"Device {slug} {field} must be an https URL")

        device_yaml_path = check_relative_path(device.get("device_yaml"), f"Device {slug} device_yaml", errors)
        package_yaml_path = check_relative_path(device.get("package_yaml"), f"Device {slug} package_yaml", errors)
        if not device_yaml_path or not package_yaml_path:
            continue
        device_yaml = read(ROOT / device_yaml_path, errors)
        package_yaml = read(ROOT / package_yaml_path, errors)
        package_dir = (ROOT / package_yaml_path).parent
        package_include_paths = {
            str(include.get("alias", "")).strip(): str(include.get("path", "")).strip()
            for include in package_includes
            if isinstance(include, dict)
        } if isinstance(package_includes, list) else {}

        for field, needle in (
            ("esp32_variant", f'variant: {device.get("esp32_variant", "")}'),
            ("flash_size", f'flash_size: {device.get("flash_size", "")}'),
            ("framework_type", f'type: {device.get("framework_type", "")}'),
            ("platformio_flash_mode", f'board_build.flash_mode: {device.get("platformio_flash_mode", "")}'),
            ("esp32_hosted_variant", f'variant: {device.get("esp32_hosted_variant", "")}'),
            ("psram_mode", f'mode: {device.get("psram_mode", "")}'),
            ("psram_speed", f'speed: {device.get("psram_speed", "")}'),
            ("touch_platform", f'platform: {device.get("touch_platform", "")}'),
        ):
            if str(device.get(field, "")).strip():
                require_contains(device_yaml, needle, rel(ROOT / device_yaml_path), errors)
        _require_display_panel_contract(device, device_yaml, rel(ROOT / device_yaml_path), errors)
        for field, needle in (
            ("engineering_sample", "engineering_sample: true"),
            ("idf_experimental_features", "enable_idf_experimental_features: true"),
        ):
            if device.get(field) is True:
                require_contains(device_yaml, needle, rel(ROOT / device_yaml_path), errors)
        if isinstance(sdkconfig_options, dict):
            for option, value in sdkconfig_options.items():
                if isinstance(option, str) and isinstance(value, str) and option.strip() and value.strip():
                    require_contains(device_yaml, f'{option}: "{value}"', rel(ROOT / device_yaml_path), errors)
        if isinstance(build_flags, list):
            for flag in build_flags:
                if isinstance(flag, str) and flag.strip():
                    require_contains(device_yaml, f'- "{flag}"', rel(ROOT / device_yaml_path), errors)

        native_width = device.get("display_native_width")
        native_height = device.get("display_native_height")
        ui_width = device.get("display_ui_width")
        ui_height = device.get("display_ui_height")
        if isinstance(native_width, int):
            require_contains(device_yaml, f"width: {native_width}", rel(ROOT / device_yaml_path), errors)
        if isinstance(native_height, int):
            require_contains(device_yaml, f"height: {native_height}", rel(ROOT / device_yaml_path), errors)
        if isinstance(ui_width, int):
            require_contains(package_yaml, f'display_width: "{ui_width}"', rel(ROOT / package_yaml_path), errors)
        if isinstance(ui_height, int):
            require_contains(package_yaml, f'display_height: "{ui_height}"', rel(ROOT / package_yaml_path), errors)
        if isinstance(hardware_pins, dict):
            pin_markers = {
                "esp32_hosted_reset": "reset_pin",
                "esp32_hosted_cmd": "cmd_pin",
                "esp32_hosted_clk": "clk_pin",
                "esp32_hosted_d0": "d0_pin",
                "esp32_hosted_d1": "d1_pin",
                "esp32_hosted_d2": "d2_pin",
                "esp32_hosted_d3": "d3_pin",
                "backlight_pwm": "pin",
                "display_reset": "reset_pin",
                "touch_reset": "reset_pin",
                "touch_interrupt": "interrupt_pin",
                "i2c_sda": "sda",
                "i2c_scl": "scl",
            }
            for pin_name, marker in pin_markers.items():
                pin = str(hardware_pins.get(pin_name, "")).strip()
                if pin:
                    require_contains(device_yaml, f"{marker}: {pin}", rel(ROOT / device_yaml_path), errors)
        if isinstance(power_rail, dict):
            rail_id = str(power_rail.get("id", "")).strip()
            rail_voltage = str(power_rail.get("voltage", "")).strip()
            rail_channel = power_rail.get("channel")
            if rail_id:
                require_contains(device_yaml, f"id: {rail_id}", rel(ROOT / device_yaml_path), errors)
            if isinstance(rail_channel, int):
                require_contains(device_yaml, f"channel: {rail_channel}", rel(ROOT / device_yaml_path), errors)
            if rail_voltage:
                require_contains(device_yaml, f"voltage: {rail_voltage}", rel(ROOT / device_yaml_path), errors)
        i2c_frequency = str(device.get("i2c_frequency", "")).strip()
        if i2c_frequency:
            require_contains(device_yaml, f"frequency: {i2c_frequency}", rel(ROOT / device_yaml_path), errors)
        if isinstance(package_includes, list):
            for include in package_includes:
                if not isinstance(include, dict):
                    continue
                alias = str(include.get("alias", "")).strip()
                path = str(include.get("path", "")).strip()
                if not alias or not path:
                    continue
                require_contains(package_yaml, f"{alias}:", rel(ROOT / package_yaml_path), errors)
                require_contains(package_yaml, f"!include {path}", rel(ROOT / package_yaml_path), errors)
                include_path = (package_dir / path).resolve()
                if not include_path.is_file():
                    errors.append(f"Missing package include for device {slug}: {rel(include_path)}")
        if isinstance(package_substitutions, dict):
            for name, value in package_substitutions.items():
                if isinstance(name, str) and isinstance(value, str) and name.strip() and value.strip():
                    require_contains(package_yaml, f'{name}: "{value}"', rel(ROOT / package_yaml_path), errors)
        lvgl_base_include = package_include_paths.get("lvgl_base", "")
        if lvgl_base_include:
            lvgl_base_path = (package_dir / lvgl_base_include).resolve()
            lvgl_base_label = rel(lvgl_base_path)
            lvgl_base_yaml = read(lvgl_base_path, errors)
            lvgl_buffer_size = str(device.get("lvgl_buffer_size", "")).strip()
            lvgl_byte_order = str(device.get("lvgl_byte_order", "")).strip()
            lvgl_rotation_substitution = str(device.get("lvgl_rotation_substitution", "")).strip()
            if lvgl_buffer_size:
                require_contains(lvgl_base_yaml, f"buffer_size: {lvgl_buffer_size}", lvgl_base_label, errors)
            if lvgl_byte_order:
                require_contains(lvgl_base_yaml, f"byte_order: {lvgl_byte_order}", lvgl_base_label, errors)
            if lvgl_rotation_substitution:
                require_contains(lvgl_base_yaml, f"rotation: ${{{lvgl_rotation_substitution}}}", lvgl_base_label, errors)
            if device.get("lvgl_resume_on_input") is True:
                require_contains(lvgl_base_yaml, "resume_on_input: true", lvgl_base_label, errors)
            require_contains(lvgl_base_yaml, "displays:", lvgl_base_label, errors)
            require_contains(lvgl_base_yaml, "- tft_display", lvgl_base_label, errors)
        fonts_include = package_include_paths.get("fonts", "")
        if fonts_include:
            fonts_label = rel((package_dir / fonts_include).resolve())
            fonts_yaml = read((package_dir / fonts_include).resolve(), errors)
            if isinstance(font_assets, list):
                for font in font_assets:
                    if not isinstance(font, dict):
                        continue
                    font_id = str(font.get("id", "")).strip()
                    font_file = str(font.get("file", "")).strip()
                    font_size = font.get("size")
                    font_bpp = font.get("bpp")
                    if font_file:
                        require_contains(fonts_yaml, f'file: "{font_file}"', fonts_label, errors)
                    if font_id:
                        require_contains(fonts_yaml, f"id: {font_id}", fonts_label, errors)
                    if isinstance(font_size, int):
                        require_contains(fonts_yaml, f"size: {font_size}", fonts_label, errors)
                    if isinstance(font_bpp, int):
                        require_contains(fonts_yaml, f"bpp: {font_bpp}", fonts_label, errors)
            if isinstance(location_font_extra_files, list):
                for font_file in location_font_extra_files:
                    if isinstance(font_file, str) and font_file.strip():
                        require_contains(fonts_yaml, f'file: "{font_file.strip()}"', fonts_label, errors)
            for needle in (
                "latin_extended_glyphs:",
                "greek_cyrillic_vietnamese_glyphs:",
                "arabic_location_glyphs:",
                "hebrew_location_glyphs:",
                "glyphsets:",
                "GF_Latin_Core",
                "GF_Latin_Beyond",
                "GF_Latin_Vietnamese",
                "GF_Greek_Core",
                "GF_Cyrillic_Plus",
                "extras:",
            ):
                require_contains(fonts_yaml, needle, fonts_label, errors)
        icons_include = package_include_paths.get("icons", "")
        if icons_include:
            icons_label = rel((package_dir / icons_include).resolve())
            icons_yaml = read((package_dir / icons_include).resolve(), errors)
            if isinstance(icon_font, dict):
                icon_font_id = str(icon_font.get("id", "")).strip()
                icon_font_file = str(icon_font.get("file", "")).strip()
                icon_font_size = icon_font.get("size")
                icon_font_bpp = icon_font.get("bpp")
                if icon_font_file:
                    require_contains(icons_yaml, icon_font_file, icons_label, errors)
                if icon_font_id:
                    require_contains(icons_yaml, f"id: {icon_font_id}", icons_label, errors)
                if isinstance(icon_font_size, int):
                    require_contains(icons_yaml, f"size: {icon_font_size}", icons_label, errors)
                if isinstance(icon_font_bpp, int):
                    require_contains(icons_yaml, f"bpp: {icon_font_bpp}", icons_label, errors)
            if isinstance(icon_substitutions, dict):
                for name, glyph in icon_substitutions.items():
                    if isinstance(name, str) and isinstance(glyph, str) and name.strip() and glyph.strip():
                        require_contains(icons_yaml, f'{name}: "{glyph}"', icons_label, errors)
                        require_contains(icons_yaml, f'- "{glyph}"', icons_label, errors)
        for needle in (
            "esp_ldo:",
            "platform: ledc",
            "id: backlight_output",
            "i2c:",
            "scan: true",
            "sda_pullup_enabled: false",
            "scl_pullup_enabled: false",
            "auto_clear_enabled: false",
            "color_order: RGB",
        ):
            require_contains(device_yaml, needle, rel(ROOT / device_yaml_path), errors)
