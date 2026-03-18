---
title: Photo Sources
description: Choose where your frame gets its images — all photos, favorites, albums, people, or memories — and how to set each source up.
---

# Photo Sources

The frame can display photos from your Immich library in several ways. You choose the **Source** in the device web UI at `http://<device-ip>/` under **Photo Source**. Some sources need extra setup (album or person IDs); others work as soon as you select them.

## Overview

| Source | Extra setup | Best for |
|--------|-------------|----------|
| **All Photos** | None | Whole library, maximum variety |
| **Favorites** | Mark favorites in Immich | Curated highlights only |
| **Album** | One or more album UUIDs | Specific albums (e.g. “Family 2024”) |
| **Person** | One or more person UUIDs | Photos of specific people |
| **Memories** | None | “On this day” from past years |

Changes to the photo source are applied automatically: the frame flushes its cache and starts loading from the new source within a few seconds.

---

## All Photos

**What it does:** Shows random photos from your entire Immich library (all images you have access to).

**How to set it up:**

1. Open the frame’s web UI: `http://<device-ip>/`.
2. Under **Photo Source**, set **Source** to **All Photos**.
3. Leave **Album IDs** and **Person IDs** empty.
4. No other configuration is required.

Use this when you want maximum variety and don’t need to limit by album, person, or favorites.

---

## Favorites

**What it does:** Shows only photos you’ve marked as favorites (heart) in Immich.

**How to set it up:**

1. In Immich (web or app), open any photo and tap or click the **heart** to mark it as a favorite. Repeat for all photos you want on the frame.
2. Open the frame’s web UI: `http://<device-ip>/`.
3. Under **Photo Source**, set **Source** to **Favorites**.
4. Leave **Album IDs** and **Person IDs** empty.

If you have no favorites, the frame may show no images or fall back depending on device behavior. Make sure at least some photos are favorited in Immich.

---

## Album

**What it does:** Shows only photos that belong to one or more specific Immich albums (e.g. “Holiday 2024”, “Kids”).

**How to set it up:**

1. **Create or use albums in Immich**  
   In the Immich web UI, go to **Albums**, create an album if needed, and add the photos you want.

2. **Get the album UUID**  
   - Open the album in Immich (click the album to open it).  
   - Look at the browser URL. It will look like:  
     `http://immich.local:2283/albums/xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`  
   - The **album UUID** is the last part: `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`.  
   - Copy that full UUID (including hyphens).

3. **Enter it on the frame**  
   - Open the frame’s web UI: `http://<device-ip>/`.  
   - Under **Photo Source**, set **Source** to **Album**.  
   - In **Album IDs**, paste the UUID.  
   - For **multiple albums**, paste several UUIDs separated by commas (e.g. `uuid1,uuid2,uuid3`). No spaces are required; the frame accepts comma-separated list.

4. Save. The frame will only show photos from those albums.

::: tip Finding album UUIDs
If the URL doesn’t show the UUID clearly, open the album, then check the address bar. The UUID is always the last path segment in the album page URL.
:::

---

## Person

**What it does:** Shows only photos where one or more specific people (faces) are recognized in Immich.

**How to set it up:**

1. **Use face recognition in Immich**  
   Immich must have faces detected and people created. In the Immich web UI, go to **People** and ensure the people you want are listed and have photos assigned.

2. **Get the person UUID**  
   - Open the person’s page in Immich (click the person under **People**).  
   - Look at the browser URL. It will look like:  
     `http://immich.local:2283/person/xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`  
   - The **person UUID** is the last part: `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`.  
   - Copy that full UUID.

3. **Enter it on the frame**  
   - Open the frame’s web UI: `http://<device-ip>/`.  
   - Under **Photo Source**, set **Source** to **Person**.  
   - In **Person IDs**, paste the UUID.  
   - For **multiple people**, paste several UUIDs separated by commas (e.g. `uuid1,uuid2,uuid3`).

4. Save. The frame will only show photos that contain at least one of those people.

::: info API key and people
Your [API key](/api-key) should have `person.read` permission so the frame can use the Person source and show people’s names on the overlay when supported.
:::

---

## Memories

**What it does:** Shows “On this day” photos — images taken on the same calendar date in previous years — similar to the Memories feature in the Immich app. If there are no memories for today, the frame falls back to random photos from your library.

**How to set it up:**

1. No IDs are required. Memories use the current date on the device (based on the timezone you set in the frame’s **Clock** settings).
2. Open the frame’s web UI: `http://<device-ip>/`.
3. Under **Photo Source**, set **Source** to **Memories**.
4. Ensure your [API key](/api-key) has **memory.read** permission so the frame can call the Immich memories API.

Make sure the frame’s **Timezone** (under **Clock**) is correct so “today” matches your local date. Memories are chosen by the device’s current date.

---

# Frequency

The **Frequency** settings control how often the frame advances to the next photo. All options are available in the device web UI at `http://<device-ip>/` under the **Frequency** card.

## Slideshow Interval

| Setting | Default | Description |
|---------|---------|-------------|
| **Slideshow Interval** | 15 seconds | Time each photo is displayed before advancing to the next |