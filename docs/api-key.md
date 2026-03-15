---
title: Immich API Key
description: Create a scoped Immich API key with minimal read-only permissions for your Espframe device.
---

# Immich API Key

In your Immich web UI, go to **Account Settings → API Keys → New API Key**.

Immich lets you scope API keys to specific permissions. Espframe only needs read access — it never modifies, deletes, or uploads anything in your library. Deselect all permissions first, then enable only the ones below.

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