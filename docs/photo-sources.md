---
title: Photo Sources
description: Choose where your frame gets its images — all photos, favorites, albums, people, or memories — and how to set each source up.
---

# Photo Sources

Choose the **Source** in the device web UI at `http://<device-ip>/` under **Photo Source**. Changes apply automatically within a few seconds.

| Source | Extra setup | Best for |
|--------|-------------|----------|
| **All Photos** | None | Whole library |
| **Favorites** | Mark favorites in Immich | Curated highlights |
| **Album** | One or more album UUIDs | Specific albums |
| **Person** | One or more person UUIDs | Photos of specific people |
| **Memories** | None | "On this day" from past years |

---

## All Photos

Shows random photos from your entire library. Set **Source** to **All Photos**; leave Album IDs and Person IDs empty.

## Favorites

Shows only photos marked with the heart in Immich. Set **Source** to **Favorites**. Ensure at least some photos are favorited.

## Album

Shows photos from one or more Immich albums. **Get the UUID:** open the album in Immich — the URL is `.../albums/<uuid>`. Paste that UUID into **Album IDs** (comma-separated for multiple). Save.

## Person

Shows photos where specific people (faces) appear. Requires face recognition in Immich. **Get the UUID:** open the person under **People** — the URL is `.../person/<uuid>`. Paste into **Person IDs** (comma-separated for multiple). With several IDs, each new image is chosen from **one** of those people at random, so you see photos featuring **any** of them (not only photos where everyone appears together). Your [API key](/api-key) needs `person.read`.

## Memories

Shows "On this day" photos from past years; falls back to random if none. Set **Source** to **Memories**. No IDs needed. API key needs **memory.read**. Set **Timezone** (Clock) correctly so "today" matches.

---

## Advanced Filters

The **Advanced Filters** card in the web UI lets you restrict displayed photos to a date range. Both fields are optional — leave a field empty for no limit in that direction.

| Setting | Default | Format | Description |
|---------|---------|--------|-------------|
| **Minimum Date** | *(empty)* | `YYYY-MM-DD` | Only show photos taken on or after this date. Leave empty for no lower bound. |
| **Maximum Date** | *(empty)* | `YYYY-MM-DD` | Only show photos taken on or before this date. Leave empty for no upper bound. |

Click **Apply** after changing either value. When both dates are set, the minimum date must not be after the maximum date.

::: tip
You can set only one side of the range — for example, a **Minimum Date** with no **Maximum Date** shows everything from that date onward.
:::

---

## Frequency

**Slideshow Interval** (under **Frequency** in the web UI) sets how long each photo is shown before advancing (default 2 minutes, minimum 30 seconds).

**Connection Timeout** sets how long the frame waits without successfully displaying a new photo before showing the connection-failed screen (default 2 minutes, range 30 seconds – 30 minutes). Increase this if you have a slow server or large photo library and see false disconnects.
