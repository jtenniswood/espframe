---
title: Immich API Key Permissions for Espframe
description: Create a read-only Immich API key with the minimum permissions needed for an Espframe digital photo frame.
---

# Immich API Key Permissions for Espframe

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

## Next steps

After creating the key, add the **Immich server URL** and paste this key in the frame’s web UI (or during [Install](/install)). The URL can use `http://` or `https://`, and can be either an IP address or a domain name. Then choose a [Photo Source](/photo-sources) and your photos will start loading.
