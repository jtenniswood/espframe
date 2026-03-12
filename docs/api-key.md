# Creating an API Key

In your Immich web UI, go to **Account Settings → API Keys → New API Key**.

Immich lets you scope API keys to specific permissions. espframe only needs read access — it never modifies, deletes, or uploads anything in your library. Deselect all permissions first, then enable only the ones below.

## Required permissions

| Permission | Why |
|---|---|
| `asset.read` | Search for random photos and read metadata (date, location, EXIF) |
| `asset.view` | Download photo thumbnails for display |
| `person.read` | Show people's names on the photo overlay |

These three permissions are all the frame needs to function.

## Optional permissions for future features

If you'd like to future-proof your key for richer metadata display, you can also enable these read-only permissions:

| Permission | What it unlocks |
|---|---|
| `album.read` | Album names a photo belongs to |
| `tag.read` | Tags assigned to photos |
| `memory.read` | "On this day" memories and groupings |
| `map.read` | Additional GPS/map data beyond EXIF |

::: tip Principle of least privilege
You can always create a new API key later with additional permissions. Start with just the three required ones and expand if needed.
:::
