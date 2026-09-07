#!/usr/bin/env python3
"""Runtime policy must distinguish shipped assets from downloaded build caches."""
import sys
import tempfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import check_source_ownership as ownership


def test_runtime_policy_ignores_cache_but_rejects_shipped_dependency():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        slideshow = root / "common/addon/immich_slideshow.yaml"
        slideshow.parent.mkdir(parents=True)
        slideshow.write_text('immich_image_initial_url: "http://127.0.0.1/espframe/image-not-loaded"')
        device = root / "devices/example"
        cached = device / ".esphome/font/font.css"
        cached.parent.mkdir(parents=True)
        cached.write_text("https://fonts.gstatic.com/cached-font.woff2")
        policy = {"runtime_network_policy": {"remote_image_initial_url": "http://127.0.0.1/espframe/image-not-loaded"}}
        with patch.object(ownership, "ROOT", root):
            errors = []
            ownership.check_runtime_policy(policy, errors)
            assert errors == [], errors
            for relative_path in ("devices/example/assets/font.css", "docs/public/webserver/style.css", "docs/webserver/src/app.ts"):
                shipped = root / relative_path
                shipped.parent.mkdir(parents=True, exist_ok=True)
                shipped.write_text("https://fonts.gstatic.com/runtime-font.woff2")
                errors = []
                ownership.check_runtime_policy(policy, errors)
                assert len(errors) == 1 and relative_path in errors[0], errors
                shipped.unlink()


if __name__ == "__main__":
    test_runtime_policy_ignores_cache_but_rejects_shipped_dependency()
    print("source ownership tests passed")
