from __future__ import annotations

from pathlib import Path

from product_contract.common import (
    ROOT,
    check_relative_path,
    read,
    require_contains,
)
from product_config import (
    default_public_manifest_urls,
    device_public_manifest_urls,
    public_base_url,
    public_url,
)


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
    require_contains(docs_workflow, "PUBLIC_BASE_URL", ".github/workflows/docs.yml", errors)
    require_contains(docs_workflow, '--base-url "$PUBLIC_BASE_URL"', ".github/workflows/docs.yml", errors)

    urls_by_slug = device_public_manifest_urls(product)
    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        package_yaml = check_relative_path(device.get("package_yaml"), f"Device {slug} package_yaml", errors)
        if not package_yaml:
            continue
        package_text = read(ROOT / package_yaml, errors)
        update_channels = product["project"].get("firmware_update_channels", [])
        if not isinstance(update_channels, list):
            update_channels = []
        for channel in update_channels:
            url = urls_by_slug.get(slug, {}).get(str(channel).strip(), "")
            if url:
                require_contains(package_text, url, package_yaml, errors)


def check_public_site_references(product: dict, errors: list[str]) -> None:
    base_url = public_base_url(product)
    docs_url = public_url("", product)
    install_url = public_url("install", product)
    web_app_path = str(product["project"].get("web_server_public_app_path", "")).strip()
    web_app_url = public_url(web_app_path, product) if web_app_path else ""
    device_js_url = str(product["project"].get("web_server_device_js_url", ""))
    project_name = str(product["project"].get("name", "")).strip()
    social_image_alt = str(product["project"].get("social_image_alt", "")).strip()
    usb_flashing_image = str(product["project"].get("usb_flashing_image", "")).strip()
    usb_flashing_image_alt = str(product["project"].get("usb_flashing_image_alt", "")).strip()
    web_installer_required_browsers = product["project"].get("web_installer_required_browsers", [])
    web_installer_required_api = str(product["project"].get("web_installer_required_api", "")).strip()
    web_installer_unsupported_browsers = product["project"].get("web_installer_unsupported_browsers", [])
    repository_url = str(product["project"].get("repository_url", "")).strip().rstrip("/")
    default_branch = str(product["project"].get("github_default_branch", "")).strip()
    support_url = str(product["project"].get("support_url", "")).strip()
    support_button_image_url = str(product["project"].get("support_button_image_url", "")).strip()
    esphome_config_mount = str(product["project"].get("esphome_config_mount", "")).strip()

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
        if default_branch:
            require_contains(license_docs, f"({repository_url}/blob/{default_branch}/LICENSE)", "docs/license.md", errors)
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
    web_installer_computer_requirement = str(product["project"].get("web_installer_computer_requirement", "")).strip()
    if web_installer_computer_requirement:
        for label, text in (
            ("README.md", readme),
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
            ("docs/troubleshooting.md", troubleshooting_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            require_contains(text, web_installer_computer_requirement, label, errors)
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
    usb_cable_requirement = str(product["project"].get("usb_cable_requirement", "")).strip()
    usb_cable_warning = str(product["project"].get("usb_cable_warning", "")).strip()
    for field_name, value in (
        ("usb_cable_requirement", usb_cable_requirement),
        ("usb_cable_warning", usb_cable_warning),
    ):
        if not value:
            continue
        for label, text in (
            ("README.md", readme),
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
            ("docs/troubleshooting.md", troubleshooting_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            require_contains(text, value, f"{label} {field_name}", errors)

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
            if esphome_config_mount:
                require_contains(readme, f"compile {esphome_config_mount}/{build_yaml}", "README.md compile example", errors)
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
            require_contains(device_text, f'js_url: "{device_js_url}"', device_yaml, errors)
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
    default_branch = str(project.get("github_default_branch", "")).strip()
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
        if default_branch:
            require_contains(config, f"pattern: '{repository_url}/edit/{default_branch}/docs/:path'", "docs/.vitepress/config.mts", errors)
    if owner_name:
        require_contains(config, f"name: '{owner_name}'", "docs/.vitepress/config.mts", errors)
    if owner_url:
        require_contains(config, f"url: '{owner_url}'", "docs/.vitepress/config.mts", errors)
