from __future__ import annotations

from product_contract.common import ROOT, read, require_contains


def check_privacy_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    connection_model = str(project.get("privacy_connection_model", "")).strip()
    network_scope = str(project.get("privacy_network_scope", "")).strip()
    no_cloud_service = str(project.get("privacy_no_cloud_service", "")).strip()
    no_extra_account = str(project.get("privacy_no_extra_account", "")).strip()
    no_uploads = str(project.get("privacy_no_uploads", "")).strip()
    no_hosted_service = str(project.get("privacy_no_hosted_service", "")).strip()

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    immich_photo_frame_docs = read(ROOT / "docs" / "immich-photo-frame.md", errors)
    ai_txt = read(ROOT / "docs" / "public" / "ai.txt", errors)

    if connection_model:
        require_contains(readme, connection_model, "README.md", errors)
        require_contains(immich_photo_frame_docs, connection_model, "docs/index.md", errors)
    if network_scope:
        require_contains(readme, network_scope, "README.md", errors)
        require_contains(immich_photo_frame_docs, network_scope, "docs/index.md", errors)
    if no_cloud_service:
        require_contains(readme, no_cloud_service, "README.md", errors)
    if no_extra_account:
        require_contains(readme, no_extra_account, "README.md", errors)
    if no_uploads:
        require_contains(immich_photo_frame_docs, no_uploads, "docs/index.md", errors)
    if no_hosted_service:
        require_contains(immich_photo_frame_docs, no_hosted_service, "docs/index.md", errors)
    for needle in ("No hub, cloud, or extra software required", "self-hosted Immich"):
        require_contains(ai_txt, needle, "docs/public/ai.txt", errors)
    for needle in ("cloud account", "separate bridge service"):
        require_contains(index_docs, needle, "docs/index.md", errors)
