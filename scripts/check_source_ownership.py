#!/usr/bin/env python3
"""Validate authored, generated, vendored, and runtime dependency boundaries."""

from __future__ import annotations

import json
import sys
from pathlib import Path

from product_config import load_product


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "product" / "source-ownership.json"


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def load_manifest(errors: list[str]) -> dict[str, object]:
    try:
        data = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"cannot read product/source-ownership.json: {exc}")
        return {}
    if not isinstance(data, dict):
        errors.append("product/source-ownership.json must contain an object")
        return {}
    return data


def require_path(raw_path: object, label: str, errors: list[str]) -> Path | None:
    if not isinstance(raw_path, str) or not raw_path.strip():
        errors.append(f"{label} must be a non-empty repository path")
        return None
    path = ROOT / raw_path
    if not path.exists():
        errors.append(f"{label} does not exist: {raw_path}")
        return None
    return path


def check_unique_paths(entries: object, label: str, errors: list[str]) -> list[dict[str, object]]:
    if not isinstance(entries, list) or not entries:
        errors.append(f"{label} must be a non-empty list")
        return []
    clean: list[dict[str, object]] = []
    seen: set[str] = set()
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            errors.append(f"{label}[{index}] must be an object")
            continue
        raw_path = entry.get("path")
        if not isinstance(raw_path, str) or not raw_path.strip():
            errors.append(f"{label}[{index}].path must be a non-empty string")
            continue
        if raw_path in seen:
            errors.append(f"{label} repeats path {raw_path}")
        seen.add(raw_path)
        clean.append(entry)
    return clean


def check_generated(manifest: dict[str, object], errors: list[str]) -> None:
    entries = check_unique_paths(manifest.get("generated"), "generated", errors)
    recorded = {str(entry["path"]) for entry in entries}
    expected = set(load_product()["project"].get("generated_asset_outputs", []))
    if recorded != expected:
        missing = sorted(expected - recorded)
        extra = sorted(recorded - expected)
        if missing:
            errors.append("source ownership is missing generated outputs: " + ", ".join(missing))
        if extra:
            errors.append("source ownership has unknown generated outputs: " + ", ".join(extra))

    for entry in entries:
        path = require_path(entry.get("path"), "generated.path", errors)
        sources = entry.get("sources")
        if not isinstance(sources, list) or not sources:
            errors.append(f"generated {entry['path']} must list sources")
        else:
            for source in sources:
                require_path(source, f"generated {entry['path']} source", errors)
        generator = require_path(entry.get("generator"), f"generated {entry['path']} generator", errors)
        if path is not None and generator is not None and generator.suffix not in {".py", ".mjs", ".js"}:
            errors.append(f"generated {entry['path']} has an unsupported generator type")


def check_vendored(manifest: dict[str, object], authored_roots: set[str], errors: list[str]) -> None:
    entries = check_unique_paths(manifest.get("vendored"), "vendored", errors)
    vendored_paths: set[str] = set()
    for entry in entries:
        raw_path = str(entry["path"])
        vendored_paths.add(raw_path)
        require_path(raw_path, "vendored.path", errors)
        license_path = require_path(entry.get("license_file"), f"vendored {raw_path} license_file", errors)
        if license_path is not None and not relative(license_path).startswith(raw_path + "/"):
            errors.append(f"vendored {raw_path} license file must live inside the vendor directory")
        upstream = entry.get("upstream")
        if not isinstance(upstream, str) or not upstream.startswith("https://"):
            errors.append(f"vendored {raw_path} must record an HTTPS upstream")
        for key in ("name", "revision", "license", "local_changes"):
            value = entry.get(key)
            if not isinstance(value, str) or not value.strip():
                errors.append(f"vendored {raw_path} must record {key}")

    component_dirs = {
        relative(path)
        for path in (ROOT / "components").iterdir()
        if path.is_dir() and not path.name.startswith(".")
    }
    classified = vendored_paths | {path for path in authored_roots if path.startswith("components/")}
    missing = sorted(component_dirs - classified)
    if missing:
        errors.append("component directories have no authored/vendor owner: " + ", ".join(missing))


def check_external_assets(manifest: dict[str, object], errors: list[str]) -> None:
    assets = manifest.get("external_build_assets")
    if not isinstance(assets, list) or not assets:
        errors.append("external_build_assets must be a non-empty list")
        return
    for index, asset in enumerate(assets):
        if not isinstance(asset, dict):
            errors.append(f"external_build_assets[{index}] must be an object")
            continue
        for key in ("usage", "upstream", "revision", "license"):
            value = asset.get(key)
            if not isinstance(value, str) or not value.strip():
                errors.append(f"external_build_assets[{index}] must record {key}")
        upstream = asset.get("upstream")
        if isinstance(upstream, str) and not upstream.startswith("https://"):
            errors.append(f"external_build_assets[{index}].upstream must use HTTPS")
        declarations = asset.get("declarations")
        if not isinstance(declarations, list) or not declarations:
            errors.append(f"external_build_assets[{index}] must list declarations")
        else:
            for declaration in declarations:
                require_path(declaration, f"external_build_assets[{index}] declaration", errors)


def check_runtime_policy(manifest: dict[str, object], errors: list[str]) -> None:
    policy = manifest.get("runtime_network_policy")
    if not isinstance(policy, dict):
        errors.append("runtime_network_policy must be an object")
        return
    initial_url = policy.get("remote_image_initial_url")
    if initial_url != "http://127.0.0.1/espframe/image-not-loaded":
        errors.append("remote_image_initial_url must remain loopback-only")

    slideshow = (ROOT / "common" / "addon" / "immich_slideshow.yaml").read_text(encoding="utf-8")
    if f'immich_image_initial_url: "{initial_url}"' not in slideshow:
        errors.append("Immich slideshow initial URL does not match runtime network policy")

    forbidden = (
        "via.placeholder.com",
        "fonts.googleapis.com",
        "fonts.gstatic.com",
        '<script src="http',
        '<link href="http',
    )
    runtime_files = [
        ROOT / "common",
        ROOT / "devices",
        ROOT / "docs" / "webserver" / "src",
        ROOT / "docs" / "public" / "webserver",
    ]
    for root in runtime_files:
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in {".yaml", ".yml", ".ts", ".js", ".css", ".html"}:
                continue
            text = path.read_text(encoding="utf-8")
            for needle in forbidden:
                if needle in text:
                    errors.append(f"runtime dependency {needle!r} is forbidden in {relative(path)}")


def main() -> int:
    errors: list[str] = []
    manifest = load_manifest(errors)
    if manifest.get("schema_version") != 1:
        errors.append("source ownership schema_version must be 1")

    roots = manifest.get("authored_roots")
    authored_roots: set[str] = set()
    if not isinstance(roots, list) or not roots:
        errors.append("authored_roots must be a non-empty list")
    else:
        for root in roots:
            path = require_path(root, "authored root", errors)
            if path is not None:
                authored_roots.add(relative(path))

    check_generated(manifest, errors)
    check_vendored(manifest, authored_roots, errors)
    check_external_assets(manifest, errors)
    check_runtime_policy(manifest, errors)

    docs = ROOT / "docs" / "source-ownership.md"
    if not docs.exists() or "product/source-ownership.json" not in docs.read_text(encoding="utf-8"):
        errors.append("docs/source-ownership.md must point to the ownership manifest")

    if errors:
        print("source ownership errors:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print("source ownership checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
