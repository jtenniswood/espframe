from __future__ import annotations

import json


def legacy_product_manifest(product: dict[str, object]) -> str:
    """Render the combined v1-era metadata file for compatibility consumers."""
    return json.dumps(product, indent=2, ensure_ascii=False) + "\n"
