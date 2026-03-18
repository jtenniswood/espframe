---
title: Immich API Key
description: Create a scoped Immich API key with minimal read-only permissions for your Espframe device.
---

# Immich API Key

Espframe needs a read-only API key; it never modifies or uploads. **Account Settings → API Keys → New API Key** in Immich. Deselect all, then enable only:

## Recommended permissions

| Permission | Why |
|---|---|
| `asset.read` | Search for random photos and read metadata (date, location, EXIF) |
| `asset.view` | Download photo thumbnails for display |
| `person.read` | Show people's names on the photo overlay |
| `album.read` | Album names a photo belongs to |
| `tag.read` | Tags assigned to photos |
| `memory.read` | "On this day" memories and groupings |
| `map.read` | Additional GPS/map data beyond EXIF |

Enter the key and Immich server URL in the device web UI. Advanced: [bake into ESPHome config](/manual-setup#pre-filling-immich-credentials).
