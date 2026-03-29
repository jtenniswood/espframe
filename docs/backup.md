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
| **Photos** | Source, album IDs, person IDs |
| **Frequency** | Slideshow interval, connection timeout |
| **Clock** | Show clock, format, timezone |
| **Screen Brightness** | Daytime brightness, nighttime brightness |
| **Screen Schedule** | Enable, on time, off time |
| **Screen Tone** | Tone adjustment, display tone, night tone adjustment, warm tone intensity, warm tone override |

Firmware version, update status, sunrise/sunset, and current brightness are **not** included — these are device-specific or read-only.

## Import

1. Open the device web UI at `http://<device-ip>/`.
2. Expand the **Backup** card.
3. Click **Import** and select a previously exported `.json` file.
4. Each setting is pushed to the device individually. The page refreshes when complete.

Partial config files work — only settings present in the file are applied; everything else stays unchanged.

## File Format

The export is a standard JSON file with a `version` field and grouped settings:

```json
{
  "version": 1,
  "exported_at": "2026-03-29T12:00:00.000Z",
  "connection": { "immich_url": "...", "api_key": "..." },
  "photos": { "source": "All Photos", "album_ids": "", "person_ids": "" },
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
    "warm_tone_override": false
  }
}
```

You can edit the file by hand before importing — useful for scripting or bulk-configuring devices.
