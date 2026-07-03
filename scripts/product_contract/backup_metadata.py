"""Validate backup-related product metadata."""

from __future__ import annotations

from product_contract.common import ROOT, WEB_APP, WEB_TEMPLATE, read, read_web_source, rel, require_contains


def check_backup_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    config_version = project.get("backup_config_version")
    filename_prefix = str(project.get("backup_filename_prefix", "")).strip()
    date_format = str(project.get("backup_filename_date_format", "")).strip()
    photo_id_limit = project.get("backup_import_photo_id_limit")
    excluded_values = project.get("backup_excluded_runtime_values", [])
    export_groups = [str(value).strip() for value in project.get("backup_export_groups", []) if str(value).strip()]
    import_write_behavior = str(project.get("backup_import_write_behavior", "")).strip()
    partial_behavior = str(project.get("backup_partial_config_behavior", "")).strip()
    invalid_photo_id_behavior = str(project.get("backup_invalid_photo_id_behavior", "")).strip()

    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    web_template = read_web_source(errors)
    web_text = read(WEB_APP, errors)

    if isinstance(config_version, int) and not isinstance(config_version, bool):
        require_contains(backup_docs, f'"version": {config_version}', "docs/backup.md", errors)
        require_contains(web_template, "version: BACKUP_CONFIG_VERSION", rel(WEB_TEMPLATE), errors)
        require_contains(web_template, "var BACKUP_CONFIG_VERSION = __ESPFRAME_BACKUP_CONFIG_VERSION__", rel(WEB_TEMPLATE), errors)
        require_contains(web_text, f"var BACKUP_CONFIG_VERSION = {config_version}", rel(WEB_APP), errors)
    if filename_prefix:
        require_contains(backup_docs, f"`{filename_prefix}{date_format}.json`", "docs/backup.md", errors)
        require_contains(web_template, f'var name = "{filename_prefix}"', rel(WEB_TEMPLATE), errors)
        require_contains(web_text, f'var name = "{filename_prefix}"', rel(WEB_APP), errors)
    if date_format:
        require_contains(backup_docs, date_format, "docs/backup.md", errors)
    if isinstance(photo_id_limit, int) and not isinstance(photo_id_limit, bool):
        require_contains(backup_docs, f"{photo_id_limit} characters", "docs/backup.md", errors)
        require_contains(web_template, f"MAX_PHOTO_ID_FIELD_LENGTH = {photo_id_limit}", rel(WEB_TEMPLATE), errors)
        require_contains(web_text, f"MAX_PHOTO_ID_FIELD_LENGTH = {photo_id_limit}", rel(WEB_APP), errors)
    if isinstance(excluded_values, list):
        for value in excluded_values:
            if isinstance(value, str) and value.strip():
                require_contains(backup_docs, value.strip(), "docs/backup.md", errors)
    for group in export_groups:
        require_contains(backup_docs, f'"{group}"', "docs/backup.md", errors)
    if import_write_behavior:
        require_contains(backup_docs, import_write_behavior, "docs/backup.md", errors)
    if partial_behavior:
        require_contains(backup_docs, partial_behavior, "docs/backup.md", errors)
    if invalid_photo_id_behavior:
        require_contains(backup_docs, invalid_photo_id_behavior, "docs/backup.md", errors)
        for label in ("Album IDs", "Album labels", "Person IDs", "Person labels", "Tag IDs", "Tag labels"):
            require_contains(web_template, f"{label} exceed {photo_id_limit} characters - not imported", rel(WEB_TEMPLATE), errors)
            require_contains(web_text, f"{label} exceed {photo_id_limit} characters - not imported", rel(WEB_APP), errors)
        for label in ("album IDs", "person IDs", "tag IDs"):
            require_contains(web_template, f"Import skipped invalid {label}", rel(WEB_TEMPLATE), errors)
            require_contains(web_text, f"Import skipped invalid {label}", rel(WEB_APP), errors)
    for message in (
        "Stable firmware URL was invalid - not imported",
    ):
        require_contains(web_template, message, rel(WEB_TEMPLATE), errors)
        require_contains(web_text, message, rel(WEB_APP), errors)

    for needle in (
        "display_mode",
        "display mode",
        "Partial config files work",
        "Settings imported successfully",
        "JSON.stringify(data, null, 2)",
        "buildBackupExportData",
        "BACKUP_SCHEMA.forEach",
        "normalizeScheduleWakeTimeout(S.schedule_wake_timeout)",
        "backupImportFieldPresent",
        "backupImportFieldValue",
        "applyBackupImportField",
        "backupEntryKey(entry)",
        "photos.album_ids",
        "photos.tag_ids",
        "firmware_updates.manifest_url",
        "clock.ntp_servers",
        "screen.schedule_wake_timeout",
    ):
        if needle in {
            "Settings imported successfully",
            "JSON.stringify(data, null, 2)",
            "buildBackupExportData",
            "BACKUP_SCHEMA.forEach",
            "normalizeScheduleWakeTimeout(S.schedule_wake_timeout)",
            "backupImportFieldPresent",
            "backupImportFieldValue",
            "applyBackupImportField",
            "backupEntryKey(entry)",
            "photos.album_ids",
            "photos.tag_ids",
            "firmware_updates.manifest_url",
            "clock.ntp_servers",
            "screen.schedule_wake_timeout",
        }:
            require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
            require_contains(web_text, needle, rel(WEB_APP), errors)
        else:
            require_contains(backup_docs, needle, "docs/backup.md", errors)
