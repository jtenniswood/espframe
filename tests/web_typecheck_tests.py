#!/usr/bin/env python3
"""Prove the gate checks implementation and generated setting types."""
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_web_types import typecheck
from asset_generation.web_bundle import web_app_source

result = typecheck(web_app_source() + '\nS.relative_amount = "wrong type";\nS.nonexistent_review_setting = true;\n', capture_output=True, text=True)
assert result.returncode != 0, "invalid application state unexpectedly passed"
assert "not assignable to type 'number'" in result.stdout, result.stdout
assert "nonexistent_review_setting" in result.stdout, result.stdout
print("web typecheck regression tests passed")
