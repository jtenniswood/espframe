"""Validate GitHub workflow and release contract metadata."""

from __future__ import annotations

import re
from pathlib import Path

from product_contract.common import ROOT, check_relative_path, read, rel, require_contains
from product_config import public_base_url, release_matrix_devices


def require_workflow_path_filter(text: str, path: str, label: str, errors: list[str]) -> None:
    needles = (f'- "{path}"', f"- '{path}'", f"- {path}")
    if not any(needle in text for needle in needles):
        errors.append(f"{label} is missing workflow path filter {path!r}")


def require_workflow_needs(text: str, dependencies: list[str], label: str, errors: list[str]) -> None:
    if len(dependencies) == 1:
        needle = f"    needs: {dependencies[0]}"
    else:
        needle = f"    needs: [{', '.join(dependencies)}]"
    require_contains(text, needle, label, errors)


def check_device_workflow_contract(product: dict, errors: list[str]) -> None:
    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)
    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    project = product["project"]
    release_actions = project.get("release_workflow_actions", {})
    artifact_prefix = str(project.get("release_artifact_prefix", "")).strip()
    release_build_output_dir = str(project.get("release_build_output_dir", "")).strip()
    release_publish_dir = str(project.get("release_publish_dir", "")).strip()
    release_uploaded_verify_dir = str(project.get("release_uploaded_verify_dir", "")).strip()
    release_source_factory_binary = str(project.get("release_source_factory_binary", "")).strip()
    release_source_ota_binary = str(project.get("release_source_ota_binary", "")).strip()
    release_esphome_cache_dir = str(project.get("release_esphome_cache_dir", "")).strip()
    release_esphome_cache_key_prefix = str(project.get("release_esphome_cache_key_prefix", "")).strip()
    release_esphome_cache_hash_files = [
        str(path).strip() for path in project.get("release_esphome_cache_hash_files", []) if str(path).strip()
    ]
    asset_suffixes = [str(value).strip() for value in project.get("release_asset_suffixes", []) if str(value).strip()]
    binary_download_patterns = [
        str(value).strip() for value in project.get("release_binary_download_patterns", []) if str(value).strip()
    ]
    manifest_download_patterns = [
        str(value).strip() for value in project.get("release_manifest_download_patterns", []) if str(value).strip()
    ]
    uploaded_verify_patterns = [
        str(value).strip() for value in project.get("release_uploaded_verify_patterns", []) if str(value).strip()
    ]
    release_download_clobber = project.get("github_release_download_clobber")
    release_upload_clobber = project.get("github_release_upload_clobber")
    release_version_pattern = str(project.get("release_version_pattern", "")).strip()
    stable_release_version_pattern = str(project.get("stable_release_version_pattern", "")).strip()
    firmware_version_placeholder = str(project.get("firmware_version_placeholder_line", "")).rstrip("\n")
    local_build_version = str(project.get("firmware_local_build_version", "")).strip()
    placeholder_versions = [str(value).strip() for value in project.get("firmware_placeholder_versions", []) if str(value).strip()]
    changelog_categories = project.get("release_changelog_categories", [])
    changelog_fallback = str(project.get("release_changelog_fallback_category", "")).strip()
    docs_dist_artifact_name = str(project.get("docs_dist_artifact_name", "")).strip()
    docs_firmware_artifact_name = str(project.get("docs_firmware_artifact_name", "")).strip()
    compile_firmware_artifact_prefix = str(project.get("compile_firmware_artifact_prefix", "")).strip()
    compile_firmware_output_dir = str(project.get("compile_firmware_output_dir", "")).strip()
    compile_firmware_version_prefix = str(project.get("compile_firmware_version_prefix", "")).strip()
    docs_dist_output_path = str(project.get("docs_dist_output_path", "")).strip()
    docs_deploy_path = str(project.get("docs_deploy_path", "")).strip()
    pages_environment = str(project.get("github_pages_environment", "")).strip()
    pages_concurrency_group = str(project.get("github_pages_concurrency_group", "")).strip()
    pages_cancel_in_progress = project.get("github_pages_cancel_in_progress")
    docs_workflow_success_conclusion = str(project.get("github_docs_workflow_run_success_conclusion", "")).strip()
    release_notes_fetch_depth = project.get("github_release_notes_fetch_depth")
    release_notes_fetch_tags = project.get("github_release_notes_fetch_tags")
    release_notes_version_ref = str(project.get("github_release_notes_version_ref", "")).strip()
    release_build_version_ref = str(project.get("github_release_build_version_ref", "")).strip()
    release_build_ref = str(project.get("github_release_build_ref", "")).strip()
    release_build_fail_fast = project.get("github_release_build_fail_fast")
    release_notes_output = str(project.get("github_release_notes_output", "")).strip()
    sparse_checkout_files = [
        str(path).strip() for path in project.get("github_sparse_checkout_files", []) if str(path).strip()
    ]
    sparse_checkout_cone_mode = project.get("github_sparse_checkout_cone_mode")
    docs_verify_retries = project.get("docs_firmware_verify_retries")
    docs_verify_delay = project.get("docs_firmware_verify_delay_seconds")
    docs_release_meta_step_id = str(project.get("github_docs_release_meta_step_id", "")).strip()
    docs_release_tag_env = str(project.get("github_docs_release_tag_env", "")).strip()
    docs_release_tag_output = str(project.get("github_docs_release_tag_output", "")).strip()
    docs_prerelease_tag_env = str(project.get("github_docs_prerelease_tag_env", "")).strip()
    pages_deployment_step_id = str(project.get("github_pages_deployment_step_id", "")).strip()
    pages_url_output = str(project.get("github_pages_url_output", "")).strip()
    prerelease_lookup_limit = project.get("github_prerelease_lookup_limit")
    actions_runner = str(project.get("github_actions_runner", "")).strip()
    github_cli_env = project.get("github_cli_env", {})
    firmware_compile_timeout = project.get("firmware_compile_timeout_minutes")
    esphome_config_mount = str(project.get("esphome_config_mount", "")).strip()
    slugs = [str(device.get("slug", "")).strip() for device in product["devices"]]
    expected_slugs = " ".join(slugs)
    if actions_runner:
        for label, text in (
            (".github/workflows/release.yml", release_workflow),
            (".github/workflows/docs.yml", docs_workflow),
            (".github/workflows/compile.yml", compile_workflow),
        ):
            require_contains(text, f"runs-on: {actions_runner}", label, errors)
    if isinstance(release_notes_fetch_depth, int) and not isinstance(release_notes_fetch_depth, bool):
        require_contains(release_workflow, f"fetch-depth: {release_notes_fetch_depth}", ".github/workflows/release.yml", errors)
    if isinstance(release_notes_fetch_tags, bool):
        require_contains(
            release_workflow,
            f"fetch-tags: {str(release_notes_fetch_tags).lower()}",
            ".github/workflows/release.yml",
            errors,
        )
    if release_build_ref:
        require_contains(release_workflow, f"ref: {release_build_ref}", ".github/workflows/release.yml", errors)
    if isinstance(release_build_fail_fast, bool):
        require_contains(
            release_workflow,
            f"fail-fast: {str(release_build_fail_fast).lower()}",
            ".github/workflows/release.yml",
            errors,
        )
    if release_notes_version_ref:
        require_contains(release_workflow, f"VERSION: {release_notes_version_ref}", ".github/workflows/release.yml", errors)
    if release_build_version_ref:
        require_contains(release_workflow, f"VERSION: {release_build_version_ref}", ".github/workflows/release.yml", errors)
    if release_notes_output:
        for needle in (
            f'--output "{release_notes_output}"',
            f'--notes-file "{release_notes_output}"',
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    for label, text in (
        (".github/workflows/release.yml", release_workflow),
        (".github/workflows/docs.yml", docs_workflow),
    ):
        if isinstance(github_cli_env, dict):
            for raw_name, raw_value in github_cli_env.items():
                name = str(raw_name).strip()
                value = str(raw_value).strip()
                if name and value:
                    require_contains(text, f"{name}: {value}", label, errors)
        if label == ".github/workflows/release.yml":
            require_contains(text, "device_slugs: ${{ steps.product.outputs.device_slugs }}", label, errors)
            require_contains(text, "DEVICE_SLUGS: ${{ needs.release-metadata.outputs.device_slugs }}", label, errors)
        else:
            require_contains(text, "python3 scripts/product_config.py github-env >> \"$GITHUB_ENV\"", label, errors)
            require_contains(text, "$DEVICE_SLUGS", label, errors)
        if f"DEVICE_SLUGS: {expected_slugs}" in text:
            errors.append(f"{label} must read DEVICE_SLUGS from product metadata, not a literal device list")
        if sparse_checkout_files:
            require_contains(text, "sparse-checkout: |", label, errors)
            for path in sparse_checkout_files:
                require_contains(text, f"            {path}", label, errors)
        if isinstance(sparse_checkout_cone_mode, bool):
            require_contains(text, f"sparse-checkout-cone-mode: {str(sparse_checkout_cone_mode).lower()}", label, errors)
    if isinstance(release_actions, dict):
        for action in release_actions.values():
            if not isinstance(action, str) or not action.strip():
                continue
            if (
                "download-artifact" in action
                or "upload-artifact" in action
                or "cache" in action
                or "upload-pages-artifact" in action
                or "deploy-pages" in action
                or "setup-node" in action
            ):
                if "upload-pages-artifact" in action or "deploy-pages" in action or "setup-node" in action:
                    require_contains(docs_workflow, action.strip(), ".github/workflows/docs.yml", errors)
                    continue
                require_contains(release_workflow, action.strip(), ".github/workflows/release.yml", errors)
            elif "checkout" in action:
                for label, text in (
                    (".github/workflows/release.yml", release_workflow),
                    (".github/workflows/docs.yml", docs_workflow),
                    (".github/workflows/compile.yml", compile_workflow),
                ):
                    require_contains(text, action.strip(), label, errors)
    if artifact_prefix:
        require_contains(release_workflow, f"name: {artifact_prefix}${{{{ matrix.slug }}}}", ".github/workflows/release.yml", errors)
        require_contains(release_workflow, f"pattern: {artifact_prefix}*", ".github/workflows/release.yml", errors)
    if release_build_output_dir:
        for needle in (
            f"mkdir -p {release_build_output_dir}",
            f"path: {release_build_output_dir}/",
            f'"{release_build_output_dir}/${{{{ matrix.slug }}}}.factory.bin"',
            f'"{release_build_output_dir}/${{{{ matrix.slug }}}}.ota.bin"',
            f'"{release_build_output_dir}/${{{{ matrix.slug }}}}.manifest.json"',
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_source_factory_binary:
        for needle in (
            f'"${{BUILD_DIR}}/{release_source_factory_binary}"',
            f"factory binary not found",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_source_ota_binary:
        for needle in (
            '-s firmware_version "${VERSION}"',
            'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.yaml"',
            f'"${{BUILD_DIR}}/{release_source_ota_binary}"',
            f"OTA binary not found",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_publish_dir:
        for needle in (
            f"path: {release_publish_dir}",
            f"--dir {release_publish_dir}",
            f"{release_publish_dir}/* --clobber",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_uploaded_verify_dir:
        for needle in (
            f"mkdir -p {release_uploaded_verify_dir}",
            f"--dir {release_uploaded_verify_dir}",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_esphome_cache_dir:
        for needle in (
            "release_esphome_cache_dir: ${{ steps.product.outputs.release_esphome_cache_dir }}",
            "path: ${{ needs.release-metadata.outputs.release_esphome_cache_dir }}",
            'if [ -d "${RELEASE_ESPHOME_CACHE_DIR}" ]; then',
            'sudo chown -R "$USER:$USER" "${RELEASE_ESPHOME_CACHE_DIR}"',
            'chmod -R u+rwX "${RELEASE_ESPHOME_CACHE_DIR}"',
            'BUILD_DIR="${RELEASE_ESPHOME_CACHE_DIR}/build/${{ matrix.build_name }}/.pioenvs/${{ matrix.build_name }}"',
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_esphome_cache_key_prefix:
        require_contains(
            release_workflow,
            "release_esphome_cache_key_prefix: ${{ steps.product.outputs.release_esphome_cache_key_prefix }}",
            ".github/workflows/release.yml",
            errors,
        )
        require_contains(
            release_workflow,
            "restore-keys: |",
            ".github/workflows/release.yml",
            errors,
        )
        require_contains(
            release_workflow,
            "${{ needs.release-metadata.outputs.release_esphome_cache_key_prefix }}-${{ matrix.slug }}-",
            ".github/workflows/release.yml",
            errors,
        )
    if release_esphome_cache_hash_files:
        hash_files = "', '".join(release_esphome_cache_hash_files)
        require_contains(
            release_workflow,
            f"hashFiles('{hash_files}')",
            ".github/workflows/release.yml",
            errors,
        )
        require_contains(
            compile_workflow,
            f"hashFiles('{hash_files}')",
            ".github/workflows/compile.yml",
            errors,
        )
    if compile_firmware_artifact_prefix:
        require_contains(
            compile_workflow,
            "actions/upload-artifact@v7",
            ".github/workflows/compile.yml",
            errors,
        )
        require_contains(
            compile_workflow,
            f"name: {compile_firmware_artifact_prefix}${{{{ matrix.slug }}}}",
            ".github/workflows/compile.yml",
            errors,
        )
    if compile_firmware_output_dir:
        for needle in (
            f"mkdir -p {compile_firmware_output_dir}",
            f"path: {compile_firmware_output_dir}/",
            f'"{compile_firmware_output_dir}/${{{{ matrix.slug }}}}.factory.bin"',
            f'"{compile_firmware_output_dir}/${{{{ matrix.slug }}}}.ota.bin"',
            f'"{compile_firmware_output_dir}/${{{{ matrix.slug }}}}.version.txt"',
        ):
            require_contains(compile_workflow, needle, ".github/workflows/compile.yml", errors)
    if compile_firmware_version_prefix:
        require_contains(
            compile_workflow,
            f'TEST_VERSION="{compile_firmware_version_prefix}-${{GITHUB_RUN_ID}}-${{GITHUB_RUN_ATTEMPT}}"',
            ".github/workflows/compile.yml",
            errors,
        )
        require_contains(compile_workflow, '-s firmware_version "${TEST_VERSION}"', ".github/workflows/compile.yml", errors)
    for pattern in binary_download_patterns:
        require_contains(docs_workflow, f'--pattern "{pattern}"', ".github/workflows/docs.yml", errors)
    for pattern in manifest_download_patterns:
        if pattern == "manifest.json":
            require_contains(docs_workflow, 'basename "$DEFAULT_PUBLIC_MANIFEST"', ".github/workflows/docs.yml", errors)
            require_contains(docs_workflow, 'basename "$DEFAULT_PUBLIC_BETA_MANIFEST"', ".github/workflows/docs.yml", errors)
        else:
            require_contains(docs_workflow, f'--pattern "{pattern}"', ".github/workflows/docs.yml", errors)
    for pattern in uploaded_verify_patterns:
        require_contains(release_workflow, f'--pattern "{pattern}"', ".github/workflows/release.yml", errors)
    if release_download_clobber is True:
        require_contains(docs_workflow, "--clobber", ".github/workflows/docs.yml", errors)
    if release_upload_clobber is True:
        require_contains(release_workflow, f"{release_publish_dir}/* --clobber", ".github/workflows/release.yml", errors)
    for suffix in asset_suffixes:
        require_contains(release_workflow, suffix, ".github/workflows/release.yml", errors)
        if suffix == ".manifest.json":
            require_contains(docs_workflow, suffix, ".github/workflows/docs.yml", errors)
        require_contains(read(ROOT / "scripts" / "firmware_release.py", errors), suffix, "scripts/firmware_release.py", errors)
    firmware_release_script = read(ROOT / "scripts" / "firmware_release.py", errors)
    release_changelog_script = read(ROOT / "scripts" / "release_changelog.py", errors)
    if release_version_pattern:
        require_contains(firmware_release_script, "release_version_pattern", "scripts/firmware_release.py", errors)
    if stable_release_version_pattern:
        require_contains(release_changelog_script, "stable_release_version_pattern", "scripts/release_changelog.py", errors)
    if isinstance(changelog_categories, list) and changelog_categories:
        require_contains(release_changelog_script, "release_changelog_categories", "scripts/release_changelog.py", errors)
    if changelog_fallback:
        require_contains(release_changelog_script, "release_changelog_fallback_category", "scripts/release_changelog.py", errors)
    if firmware_version_placeholder:
        require_contains(firmware_release_script, "firmware_version_placeholder_line", "scripts/firmware_release.py", errors)
        for device in product["devices"]:
            build_yaml = check_relative_path(device.get("build_yaml"), f"Device {device.get('slug', '<missing>')} build_yaml", errors)
            if build_yaml:
                require_contains(read(ROOT / build_yaml, errors), firmware_version_placeholder, build_yaml, errors)
    if placeholder_versions:
        require_contains(firmware_release_script, "firmware_placeholder_versions", "scripts/firmware_release.py", errors)
    if local_build_version:
        require_contains(
            read(ROOT / "common" / "addon" / "firmware_update.yaml", errors),
            f'firmware_version: "{local_build_version}"',
            "common/addon/firmware_update.yaml",
            errors,
        )
    if docs_dist_artifact_name:
        require_contains(docs_workflow, f"name: {docs_dist_artifact_name}", ".github/workflows/docs.yml", errors)
    if docs_dist_output_path:
        require_contains(docs_workflow, f"path: {docs_dist_output_path}", ".github/workflows/docs.yml", errors)
    if docs_firmware_artifact_name:
        require_contains(docs_workflow, f"name: {docs_firmware_artifact_name}", ".github/workflows/docs.yml", errors)
        if f"mkdir -p {docs_firmware_artifact_name}" not in docs_workflow:
            require_contains(docs_workflow, 'mkdir -p "$STABLE_MANIFEST_DIR"', ".github/workflows/docs.yml", errors)
        require_contains(docs_workflow, f"path: {docs_firmware_artifact_name}/", ".github/workflows/docs.yml", errors)
        if docs_deploy_path:
            require_contains(
                docs_workflow,
                f"path: {docs_deploy_path}/{docs_firmware_artifact_name}",
                ".github/workflows/docs.yml",
                errors,
            )
            require_contains(
                docs_workflow,
                f"rm -rf {docs_deploy_path}/{docs_firmware_artifact_name}",
                ".github/workflows/docs.yml",
                errors,
            )
    if docs_deploy_path:
        require_contains(docs_workflow, f"path: {docs_deploy_path}", ".github/workflows/docs.yml", errors)
    if pages_environment:
        require_contains(docs_workflow, "environment:", ".github/workflows/docs.yml", errors)
        require_contains(docs_workflow, f"name: {pages_environment}", ".github/workflows/docs.yml", errors)
    if pages_deployment_step_id and pages_url_output:
        require_contains(docs_workflow, f"id: {pages_deployment_step_id}", ".github/workflows/docs.yml", errors)
        require_contains(
            docs_workflow,
            f"url: ${{{{ steps.{pages_deployment_step_id}.outputs.{pages_url_output} }}}}",
            ".github/workflows/docs.yml",
            errors,
        )
    if pages_concurrency_group:
        require_contains(docs_workflow, "concurrency:", ".github/workflows/docs.yml", errors)
        require_contains(docs_workflow, f"group: {pages_concurrency_group}", ".github/workflows/docs.yml", errors)
    if isinstance(pages_cancel_in_progress, bool):
        require_contains(
            docs_workflow,
            f"cancel-in-progress: {str(pages_cancel_in_progress).lower()}",
            ".github/workflows/docs.yml",
            errors,
        )
    if docs_workflow_success_conclusion:
        require_contains(
            docs_workflow,
            "github.event_name != 'workflow_run' ||",
            ".github/workflows/docs.yml",
            errors,
        )
        require_contains(
            docs_workflow,
            f"github.event.workflow_run.conclusion == '{docs_workflow_success_conclusion}'",
            ".github/workflows/docs.yml",
            errors,
        )
    if isinstance(docs_verify_retries, int) and not isinstance(docs_verify_retries, bool):
        require_contains(docs_workflow, f"--retries {docs_verify_retries}", ".github/workflows/docs.yml", errors)
    if isinstance(docs_verify_delay, int) and not isinstance(docs_verify_delay, bool):
        require_contains(docs_workflow, f"--delay {docs_verify_delay}", ".github/workflows/docs.yml", errors)
    if docs_release_tag_env:
        release_tag_ref = f"${docs_release_tag_env}"
        for needle in (
            f"{docs_release_tag_env}=$(gh release view --json tagName -q .tagName)",
            f'echo "{docs_release_tag_env}=${{{docs_release_tag_env}}}" >> "$GITHUB_ENV"',
            f'gh release download "{release_tag_ref}"',
            f'--version "{release_tag_ref}"',
        ):
            require_contains(docs_workflow, needle, ".github/workflows/docs.yml", errors)
    if docs_release_tag_env and docs_release_tag_output:
        if docs_release_meta_step_id:
            require_contains(docs_workflow, f"id: {docs_release_meta_step_id}", ".github/workflows/docs.yml", errors)
            require_contains(
                docs_workflow,
                f"{docs_release_tag_output}: ${{{{ steps.{docs_release_meta_step_id}.outputs.{docs_release_tag_output} }}}}",
                ".github/workflows/docs.yml",
                errors,
            )
        require_contains(
            docs_workflow,
            f'echo "{docs_release_tag_output}=${{{docs_release_tag_env}}}" >> "$GITHUB_OUTPUT"',
            ".github/workflows/docs.yml",
            errors,
        )
        require_contains(
            docs_workflow,
            f"${{{{ needs.download-firmware.outputs.{docs_release_tag_output} }}}}",
            ".github/workflows/docs.yml",
            errors,
        )
    if docs_prerelease_tag_env:
        prerelease_tag_ref = f"${docs_prerelease_tag_env}"
        for needle in (
            f"{docs_prerelease_tag_env}=$(gh release list",
            f'if [ -n "{prerelease_tag_ref}" ]; then',
            f'gh release download "{prerelease_tag_ref}"',
        ):
            require_contains(docs_workflow, needle, ".github/workflows/docs.yml", errors)
    if isinstance(firmware_compile_timeout, int) and not isinstance(firmware_compile_timeout, bool):
        for label, text in (
            (".github/workflows/release.yml", release_workflow),
            (".github/workflows/compile.yml", compile_workflow),
        ):
            require_contains(text, f"timeout-minutes: {firmware_compile_timeout}", label, errors)
    docs_release_lookup_needles = [
        "gh release view --json tagName",
        "python3 scripts/firmware_release.py verify-directory",
        "python3 scripts/firmware_release.py verify-pages",
        '--base-url "$PUBLIC_BASE_URL"',
    ]
    if isinstance(prerelease_lookup_limit, int) and not isinstance(prerelease_lookup_limit, bool):
        docs_release_lookup_needles.append(f"gh release list --limit {prerelease_lookup_limit} --json tagName,isPrerelease")
    for needle in docs_release_lookup_needles:
        require_contains(docs_workflow, needle, ".github/workflows/docs.yml", errors)

    try:
        release_devices = release_matrix_devices(product)
    except RuntimeError as exc:
        errors.append(str(exc))
        return

    devices_by_slug = {str(device.get("slug", "")).strip(): device for device in product["devices"]}
    require_contains(
        release_workflow,
        "release_matrix: ${{ steps.product.outputs.release_matrix }}",
        ".github/workflows/release.yml",
        errors,
    )
    require_contains(
        release_workflow,
        "matrix: ${{ fromJson(needs.release-metadata.outputs.release_matrix) }}",
        ".github/workflows/release.yml",
        errors,
    )
    for release_device in release_devices:
        slug = release_device["slug"]
        build_yaml = str(devices_by_slug.get(slug, {}).get("build_yaml", "")).strip()
        local_yaml = str(devices_by_slug.get(slug, {}).get("local_yaml", "")).strip()
        device_dir = str(Path(local_yaml).parent) if local_yaml else ""
        if esphome_config_mount:
            require_contains(
                release_workflow,
                'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.factory.yaml"',
                ".github/workflows/release.yml",
                errors,
            )
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
        if device_dir:
            require_contains(
                compile_workflow,
                f'"{device_dir}/**"',
                ".github/workflows/compile.yml",
                errors,
            )
        public_manifest_dirs = []
        for field in ("public_manifest", "public_beta_manifest"):
            public_manifest = str(devices_by_slug.get(slug, {}).get(field, "")).strip()
            if public_manifest:
                public_manifest_dirs.append(Path(public_manifest).parent.as_posix())
        for prefix in dict.fromkeys(public_manifest_dirs):
            env_name = "DEFAULT_PUBLIC_BETA_MANIFEST" if prefix.endswith("/beta") else "DEFAULT_PUBLIC_MANIFEST"
            dir_name = "BETA_MANIFEST_DIR" if prefix.endswith("/beta") else "STABLE_MANIFEST_DIR"
            require_contains(
                docs_workflow,
                f'{dir_name}=$(dirname "${env_name}")',
                ".github/workflows/docs.yml",
                errors,
            )
            require_contains(
                docs_workflow,
                f'if [ -f "${{{dir_name}}}/${{DEFAULT_DEVICE_SLUG}}.manifest.json" ]; then',
                ".github/workflows/docs.yml",
                errors,
            )
            require_contains(
                docs_workflow,
                f'cp "${{{dir_name}}}/${{DEFAULT_DEVICE_SLUG}}.manifest.json" "${env_name}"',
                ".github/workflows/docs.yml",
                errors,
            )


def check_esphome_version(product: dict, errors: list[str]) -> None:
    project = product["project"]
    version = str(project.get("esphome_version", "")).strip()
    docker_image = str(project.get("esphome_docker_image", "")).strip().rstrip(":")
    config_mount = str(project.get("esphome_config_mount", "")).strip()
    remove_container = project.get("esphome_docker_remove_container")
    if not version:
        errors.append("project.esphome_version is required")
        return

    required_refs = [
        ROOT / "README.md",
        ROOT / "docs" / "install.md",
        ROOT / "docs" / "manual-setup.md",
    ]
    for path in required_refs:
        text = read(path, errors)
        require_contains(text, version, rel(path), errors)

    readme = read(ROOT / "README.md", errors)
    if docker_image:
        require_contains(readme, f"{docker_image}:{version}", "README.md", errors)

    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    require_contains(
        compile_workflow,
        'python3 scripts/product_config.py github-output >> "$GITHUB_OUTPUT"',
        ".github/workflows/compile.yml",
        errors,
    )
    require_contains(
        compile_workflow,
        '"${ESPHOME_DOCKER_IMAGE}:${ESPHOME_VERSION}"',
        ".github/workflows/compile.yml",
        errors,
    )

    if config_mount:
        require_contains(compile_workflow, '-v "${PWD}:${ESPHOME_CONFIG_MOUNT}"', ".github/workflows/compile.yml", errors)
        require_contains(readme, f'-v "${{PWD}}:{config_mount}"', "README.md", errors)
    if remove_container is True:
        require_contains(compile_workflow, "docker run ${ESPHOME_DOCKER_REMOVE_FLAG}", ".github/workflows/compile.yml", errors)
        require_contains(readme, "docker run --rm", "README.md", errors)

    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)
    for needle in (
        "esphome_docker_image: ${{ steps.product.outputs.esphome_docker_image }}",
        "esphome_version: ${{ steps.product.outputs.esphome_version }}",
        "esphome_config_mount: ${{ steps.product.outputs.esphome_config_mount }}",
        "esphome_docker_remove_flag: ${{ steps.product.outputs.esphome_docker_remove_flag }}",
        '"${ESPHOME_DOCKER_IMAGE}:${ESPHOME_VERSION}"',
        '-v "${PWD}:${ESPHOME_CONFIG_MOUNT}"',
        "docker run ${ESPHOME_DOCKER_REMOVE_FLAG}",
    ):
        require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)


def check_workflows(product: dict, errors: list[str]) -> None:
    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    require_contains(compile_workflow, '"product/**"', ".github/workflows/compile.yml", errors)

    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)
    default_branch = str(product["project"].get("github_default_branch", "")).strip()
    if default_branch:
        require_contains(docs_workflow, f"branches: [{default_branch}]", ".github/workflows/docs.yml", errors)
    workflow_path_filters = product["project"].get("github_workflow_path_filters", {})
    if isinstance(workflow_path_filters, dict):
        for path in workflow_path_filters.get("compile_pull_request", []):
            if isinstance(path, str) and path.strip():
                require_workflow_path_filter(compile_workflow, path.strip(), ".github/workflows/compile.yml", errors)
        for path in workflow_path_filters.get("docs_push", []):
            if isinstance(path, str) and path.strip():
                require_workflow_path_filter(docs_workflow, path.strip(), ".github/workflows/docs.yml", errors)
    workflow_texts = {
        "compile": (".github/workflows/compile.yml", compile_workflow),
        "docs": (".github/workflows/docs.yml", docs_workflow),
        "release": (".github/workflows/release.yml", release_workflow),
    }
    workflow_events = product["project"].get("github_workflow_events", {})
    if isinstance(workflow_events, dict):
        for workflow, raw_events in workflow_events.items():
            workflow_name = str(workflow).strip()
            if workflow_name not in workflow_texts or not isinstance(raw_events, list):
                continue
            label, text = workflow_texts[workflow_name]
            for event in raw_events:
                event_name = str(event).strip()
                if event_name:
                    require_contains(text, f"  {event_name}:", label, errors)
    workflow_event_types = product["project"].get("github_workflow_event_types", {})
    if isinstance(workflow_event_types, dict):
        for key, raw_types in workflow_event_types.items():
            workflow_name, _, event_name = str(key).strip().partition(".")
            if workflow_name not in workflow_texts or not event_name or not isinstance(raw_types, list):
                continue
            label, text = workflow_texts[workflow_name]
            require_contains(text, f"  {event_name}:", label, errors)
            for event_type in raw_types:
                event_type_name = str(event_type).strip()
                if event_type_name:
                    require_contains(text, f"types: [{event_type_name}]", label, errors)
    workflow_jobs = product["project"].get("github_workflow_jobs", {})
    if isinstance(workflow_jobs, dict):
        for workflow, raw_jobs in workflow_jobs.items():
            workflow_name = str(workflow).strip()
            if workflow_name not in workflow_texts or not isinstance(raw_jobs, dict):
                continue
            label, text = workflow_texts[workflow_name]
            for raw_job_id, raw_job_name in raw_jobs.items():
                job_id = str(raw_job_id).strip()
                job_name = str(raw_job_name).strip()
                if job_id:
                    require_contains(text, f"  {job_id}:", label, errors)
                if job_name:
                    require_contains(text, f"    name: {job_name}", label, errors)
    workflow_job_dependencies = product["project"].get("github_workflow_job_dependencies", {})
    if isinstance(workflow_job_dependencies, dict):
        for key, raw_dependencies in workflow_job_dependencies.items():
            workflow_name, _, job_id = str(key).strip().partition(".")
            if workflow_name not in workflow_texts or not job_id or not isinstance(raw_dependencies, list):
                continue
            dependencies = [str(dependency).strip() for dependency in raw_dependencies if str(dependency).strip()]
            if dependencies:
                label, text = workflow_texts[workflow_name]
                require_contains(text, f"  {job_id}:", label, errors)
                require_workflow_needs(text, dependencies, label, errors)
    workflow_permissions = product["project"].get("github_workflow_permissions", {})
    workflow_names = product["project"].get("github_workflow_names", {})
    if isinstance(workflow_names, dict):
        for workflow, raw_name in workflow_names.items():
            workflow_name = str(workflow).strip()
            name = str(raw_name).strip()
            if workflow_name not in workflow_texts or not name:
                continue
            label, text = workflow_texts[workflow_name]
            require_contains(text, f"name: {name}", label, errors)
        release_workflow_name = str(workflow_names.get("release", "")).strip()
        if release_workflow_name:
            require_contains(docs_workflow, f'workflows: ["{release_workflow_name}"]', ".github/workflows/docs.yml", errors)
    if isinstance(workflow_permissions, dict):
        for workflow, raw_permissions in workflow_permissions.items():
            workflow_name = str(workflow).strip()
            if workflow_name not in workflow_texts or not isinstance(raw_permissions, dict):
                continue
            label, text = workflow_texts[workflow_name]
            require_contains(text, "permissions:", label, errors)
            for raw_scope, raw_access in raw_permissions.items():
                scope = str(raw_scope).strip()
                access = str(raw_access).strip()
                if scope and access:
                    require_contains(text, f"  {scope}: {access}", label, errors)
    for label, text in (
        (".github/workflows/docs.yml", docs_workflow),
        (".github/workflows/release.yml", release_workflow),
    ):
        require_contains(text, "scripts/product_config.py", label, errors)
        require_contains(text, "product/espframe.json", label, errors)


def check_node_version(product: dict, errors: list[str]) -> None:
    version = str(product["project"].get("node_version", "")).strip()
    package_cache = str(product["project"].get("node_package_cache", "")).strip()
    install_command = str(product["project"].get("node_install_command", "")).strip()
    local_check_command = str(product["project"].get("local_check_command", "")).strip()
    docs_build_command = str(product["project"].get("docs_build_command", "")).strip()
    node24_env = str(product["project"].get("github_actions_node24_env", "")).strip()
    if not version:
        errors.append("project.node_version is required")
        return
    if not re.match(r"^\d+$", version):
        errors.append("project.node_version must be a major version number")
    if version == "24" and not node24_env:
        errors.append("project.github_actions_node24_env is required when project.node_version is 24")

    node_workflow_paths = (ROOT / ".github" / "workflows" / "compile.yml", ROOT / ".github" / "workflows" / "docs.yml")
    for path in node_workflow_paths:
        text = read(path, errors)
        require_contains(text, f"node-version: {version}", rel(path), errors)
        if package_cache:
            require_contains(text, f"cache: {package_cache}", rel(path), errors)
        if install_command:
            require_contains(text, f"run: {install_command}", rel(path), errors)
        if version == "24" and node24_env:
            require_contains(text, node24_env, rel(path), errors)

    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    if local_check_command:
        require_contains(compile_workflow, f"run: {local_check_command}", ".github/workflows/compile.yml", errors)
    if docs_build_command:
        require_contains(docs_workflow, f"run: {docs_build_command}", ".github/workflows/docs.yml", errors)

    if version == "24" and node24_env:
        release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)
        require_contains(release_workflow, node24_env, ".github/workflows/release.yml", errors)
