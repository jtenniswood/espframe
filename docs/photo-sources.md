---
title: Photo Sources
description: Choose where your frame gets its images — all photos, favorites, albums, people, or memories — and how to set each source up.
---

# Photo Sources

Choose the **Source** in the device web UI at `http://<device-ip>/` under **Photo Source**, then click **Apply**.

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

## Album and Person ID limits

The device stores each of **Album IDs** and **Person IDs** as a single text field with a **255 character** maximum, which is about six full UUIDs plus commas. The web UI blocks longer lists and shows an error so values are not silently cut short.

Saving multiple IDs uses an HTTP POST body for the value, so the request stays within URL length limits and avoids errors such as **414 URI Too Long**.

## Memories

Shows "On this day" photos from past years; falls back to random if none. Set **Source** to **Memories**. No IDs needed. API key needs **memory.read**. Set **Timezone** (Clock) correctly so "today" matches.

---

## Date Filtering

Use **Advanced Filters** in the web UI to limit photos by when they were taken. You can use either fixed dates, such as a specific holiday range, or a rolling range, such as the last 6 months.

Date filter changes save automatically shortly after you change a control. You do not need to click an Apply button.

| Setting | Default | Format | Description |
|---------|---------|--------|-------------|
| **Filter by Date** | Off | Toggle | Turns date filtering on or off. When off, saved date values are ignored. |
| **Mode** | `Fixed Range` | Select | Choose whether to use fixed dates or a relative range ending today. |
| **From** | *(empty)* | `YYYY-MM-DD` | In fixed mode, only show photos taken on or after this date. Leave empty for no lower bound. |
| **Until** | *(empty)* | `YYYY-MM-DD` | In fixed mode, only show photos taken on or before this date. Leave empty for no upper bound. |
| **Last** | `1` | Number | In relative mode, the amount of time to include. |
| **Unit** | `Years` | `Months` or `Years` | In relative mode, whether the amount is counted in months or years. |

Fixed mode and relative mode are mutually exclusive, so relative ranges do not combine with the fixed From or Until dates.

### Fixed Range

Use **Fixed Range** when you want a specific window of time. For example:

- Set **From** to `2024-12-01` and **Until** to `2024-12-31` to show photos taken during December 2024.
- Leave **From** empty and set **Until** to `2020-12-31` to show photos taken up to the end of 2020.
- Set **From** to `2023-01-01` and leave **Until** empty to show photos from 2023 onwards.

### Relative Range

Use **Relative Range** when you want the filter to move forward automatically over time. Set **Last** to a number and choose **Months** or **Years**.

Examples:

- **Last** `6`, **Unit** `Months` shows photos from the last 6 months.
- **Last** `1`, **Unit** `Years` shows photos from the last year.
- **Last** `2`, **Unit** `Years` shows photos from the last 2 years.

The relative range ends on today, using the frame's configured time. Set **Timezone** under **Clock** so "today" matches your local day.

::: tip
Use relative mode for ranges like the last 6 months, last 1 year, or last 2 years so the lower bound moves forward automatically.
:::

---

## Frequency

**Slideshow Interval** (under **Frequency** in the web UI) sets how long each photo is shown before advancing (default 2 minutes, minimum 30 seconds).

**Connection Timeout** sets how long the frame waits without successfully displaying a new photo before showing the connection-failed screen (default 2 minutes, range 30 seconds – 30 minutes). Increase this if you have a slow server or large photo library and see false disconnects.
