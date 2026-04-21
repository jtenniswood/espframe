---
title: Backup & Restore
description: Export and import your Espframe settings as a JSON file to back up, restore, or clone your configuration.
---

# Backup & Restore

Export your settings to a JSON file and import them back — useful for backups, migrating to a new device, or cloning a configuration across multiple frames.

## Export

1. Open the device web UI at `http://<device-ip>/`.
2. Expand the **Backup** card.
3. Click **Export**. A file named `espframe-config-YYYY-MM-DD.json` downloads to your browser.

The export captures all user-facing settings from the current session:

| Category | Settings |
|----------|----------|
| **Connection** | Immich server URL, API key |
| **Photos** | Source, album IDs, album labels, person IDs, person labels, date filter settings, orientation, portrait pairing |
| **Frequency** | Slideshow interval, connection timeout |
| **Clock** | Show clock, format, timezone |
| **Screen Brightness** | Daytime brightness, nighttime brightness |
| **Night Schedule** | Enable, on time, off time |
| **Rotation** | Rotation |
| **Screen Tone** | Tone adjustment, display tone, night tone adjustment, warm tone intensity, warm tone override |

Firmware version, update status, sunrise/sunset, and current brightness are **not** included — these are device-specific or read-only.

## Import

1. Open the device web UI at `http://<device-ip>/`.
2. Expand the **Backup** card.
3. Click **Import** and select a previously exported `.json` file.
4. Each setting is pushed to the device individually. The page refreshes when complete.

Partial config files work — only settings present in the file are applied; everything else stays unchanged.

Album IDs, Album Labels, Person IDs, and Person Labels each must be 255 characters or fewer after trimming, which matches the device storage limit. If an import file exceeds that for any of those fields, the web UI shows an error and skips that field; other settings in the file are still sent.

## File Format

The export is a standard JSON file with a `version` field and grouped settings:

```json
{
  "version": 1,
  "exported_at": "2026-03-29T12:00:00.000Z",
  "connection": { "immich_url": "...", "api_key": "..." },
  "photos": {
    "source": "All Photos",
    "album_ids": "",
    "album_labels": "",
    "person_ids": "",
    "person_labels": "",
    "date_filter_enabled": false,
    "date_filter_mode": "Fixed Range",
    "date_from": "",
    "date_to": "",
    "relative_amount": 1,
    "relative_unit": "Years",
    "orientation": "Any",
    "portrait_pairing": true
  },
  "frequency": { "interval": "15 seconds", "conn_timeout": "2 minutes" },
  "clock": { "show": true, "format": "24 Hour", "timezone": "..." },
  "screen": {
    "brightness_day": 100,
    "brightness_night": 75,
    "schedule_enabled": false,
    "schedule_on_hour": 6,
    "schedule_off_hour": 23,
    "base_tone_enabled": false,
    "base_tone": 0,
    "warm_tones_enabled": false,
    "warm_tone_intensity": 50,
    "warm_tone_override": false,
    "rotation": "0"
  }
}
```

You can edit the file by hand before importing — useful for scripting or bulk-configuring devices.
