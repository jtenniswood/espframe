---
title: Espframe Smart Photo Filters for Immich
description: Combine Immich albums, people, tags, favorites, ratings, dates, locations, exclusions, and orientation in one photo filter.
---

# Espframe Smart Photo Filters for Immich

Open the device web UI at `http://<device-ip>/` and use **Photo Filter**. Albums, people, and tags can be enabled independently, while favorites, dates, location, rating, exclusions, and orientation act as required constraints. Changes apply automatically shortly after you change a control.

<!-- ESPFRAME:SETTINGS_TABLE source START -->
| Setting | Default | Format | Description |
|---------|---------|--------|-------------|
| **Source** | All Photos | Select | Choose all photos, favorites, albums, people, tags, or Immich memories. |
| **Albums Enabled** | False | Toggle | Include the configured albums as an independently enabled filter group. |
| **People Enabled** | False | Toggle | Include the configured people as an independently enabled filter group. |
| **Tags Enabled** | False | Toggle | Include the configured tags as an independently enabled filter group. |
| **Inclusion Groups** | Match all enabled groups | Select | Require every enabled album, people, and tag group, or rotate evenly through one enabled group per request. |
| **Album Matching** | Any selected album | Select | Choose whether an album group samples one selected album or requires all selected albums. |
| **Person Matching** | Any selected person | Select | Choose whether photos may contain any selected person or must contain all selected people. |
| **Favorites** | Any | Select | Include any photo, require favorites, or exclude favorites. |
| **Minimum Rating** | Any | Select | Require at least the selected Immich rating; available with Immich 3.2 or newer. |
| **Country** |  | Exact text, up to 96 characters | Require an exact Immich country value. |
| **State or Province** |  | Exact text, up to 96 characters | Require an exact state or province; country must also be set. |
| **City** |  | Exact text, up to 96 characters | Require an exact city; country and state or province must also be set. |
| **Album Order** | Random albums | Select | Choose whether multiple albums are sampled randomly or cycled in the order shown in the Albums list. |
| **Tag Matching** | Any selected tag | Select | Choose whether a photo may contain any selected tag or must contain every selected tag. |
<!-- ESPFRAME:SETTINGS_TABLE source END -->

| Source | Extra setup | Best for |
|--------|-------------|----------|
| **All Photos** | None | Whole library |
| **Favorites** | Mark favorites in Immich | Curated highlights |
| **Album** | One or more album UUIDs | Specific albums |
| **Person** | One or more person UUIDs | Photos of specific people |
| **Tag** | One or more tag UUIDs | Photos with specific Immich tags |
| **Custom** | Combine any controls | A composed smart filter |

---

## All Photos

Shows photos sampled across your entire Immich library. Set **Source** to **All Photos**; leave Albums, People, and Tags empty.

## Favorites

Shows only photos marked with the heart in Immich, sampled across the full favorites list. Set **Source** to **Favorites**. Ensure at least some photos are favorited.

## Album

Shows photos from one or more Immich albums. **Get the UUID:** open the album in Immich — the URL is `.../albums/<uuid>`. Paste one UUID into **Albums**, then optionally add a short description in **What is it?**. Use **Add an album** to add another album if needed.

The descriptions are saved with the IDs so the web UI can show friendly labels later. They do not affect which photos Immich returns.

Album photos are sampled through paged Immich search, so large albums are not limited to the first small batch of results.
Shared albums are sampled across photos added by every contributor, not only the account used by the frame's API key.

When you add more than one album, **Album Order** can either keep sampling albums randomly or cycle through the Albums list from top to bottom. Use the move buttons beside each album to set the list order. Photos inside each selected album are still chosen randomly.

## Person

Shows photos where specific people (faces) appear. Requires face recognition in Immich. **Get the UUID:** open the person under **People** — the URL is `.../person/<uuid>`. Paste one UUID into **People**, then optionally add the person's name in **Who is it?**. Use **Add a person** to add another person if needed. With several IDs, each new image is chosen from **one** of those people at random, so you see photos featuring **any** of them (not only photos where everyone appears together). Your [API key](/api-key) needs `person.read`.

The names are saved with the IDs so the web UI can show friendly labels later. They do not need to match the name stored in Immich.

Person photos use Immich's random search across the selected person. When you add several people, Espframe chooses one person for each photo instead of asking Immich for photos containing every selected person.

## Tag

Shows photos assigned to one or more Immich tags. **Get the UUID:** open the tag in Immich — the URL is typically `.../tags/<uuid>`. Paste one UUID into **Tags**, then optionally add a short description in **What tag is it?**. Use **Add a tag** to add another tag if needed. Your [API key](/api-key) needs `tag.read`.

Use **Tag Matching** to choose the behavior for multiple tags:

- **Any selected tag** chooses one selected tag for each photo, so the slideshow includes photos from across the selected tags.
- **All selected tags** asks Immich for photos carrying every selected tag.

This is explicit because Immich combines multiple tag IDs with AND semantics; treating a comma-separated list as an OR filter can otherwise make valid photos appear to be missing.

## Album, Person, and Tag ID limits

The device stores each of **Album IDs**, **Album Labels**, **Person IDs**, **Person Labels**, **Tag IDs**, and **Tag Labels** as a single text field with a **255 character** maximum. For IDs, that is about six full UUIDs plus commas. The web UI blocks longer lists and shows an error so values are not silently cut short.

Saving multiple IDs uses an HTTP POST body for the value, so the request stays within URL length limits and avoids errors such as **414 URI Too Long**.

## Immich compatibility

Espframe discovers the server version from Immich's public server-version endpoint. Immich 3.1 uses flat search fields. Immich 3.2 and newer use structured filters and add minimum-rating and exclusion rules. On older or unknown versions those controls are disabled, their saved values remain intact, and the frame refuses to silently omit an active unsupported rule.

The former **Memories** source is migrated to an empty filter (equivalent to All Photos) and shown once as a dismissible notice. The deprecated **Photos: Source** Home Assistant entity remains for one compatibility release as a preset adapter; selecting a legacy source resets the smart filter to that preset, while a composed filter reports **Custom**.

---

## Date Filtering

Use **Advanced Filters** in the web UI to limit photos by when they were taken. You can use either fixed dates, such as a specific holiday range, or a rolling range, such as the last 6 months.

Date filter changes save automatically shortly after you change a control. You do not need to click an Apply button.

<!-- ESPFRAME:SETTINGS_TABLE date_filtering START -->
| Setting | Default | Format | Description |
|---------|---------|--------|-------------|
| **Filter by Date** | Off | Toggle | Turns date filtering on or off. When off, saved date values are ignored. |
| **Mode** | `Fixed Range` | Select | Choose whether to use fixed dates or a relative range ending today. |
| **From** | *(empty)* | `YYYY-MM-DD` | In fixed mode, only show photos taken on or after this date. Leave empty for no lower bound. |
| **Until** | *(empty)* | `YYYY-MM-DD` | In fixed mode, only show photos taken on or before this date. Leave empty for no upper bound. |
| **Last** | `1` | Number | In relative mode, the amount of time to include. |
| **Unit** | `Years` | `Months` or `Years` | In relative mode, whether the amount is counted in months or years. |
<!-- ESPFRAME:SETTINGS_TABLE date_filtering END -->

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

## Layout

Use **Layout** to control how photos are chosen and fitted to the screen.

<!-- ESPFRAME:SETTINGS_TABLE layout START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Portrait Pairing** | On | Pairs compatible portrait photos side-by-side on landscape screens. |
| **Pairing Range** | Same Day | Choose whether portrait companions can be taken on the same day or up to one or two calendar days either side. |
| **Paired Portraits Only** | Off | Skip portrait photos unless both images in a compatible pair are ready; landscape photos are unaffected. |
| **Photo Orientation** | Any | Choose any photo, portrait-only photos, or landscape-only photos. Portrait-only is useful when the frame is mounted vertically. |
| **Display Mode** | Fill | Fill crops to cover the screen; Fit letterboxes without cropping. |
<!-- ESPFRAME:SETTINGS_TABLE layout END -->

Portrait pairing is disabled while the screen is in portrait rotation.

**Pairing Range** always checks the same calendar day first. Espframe first samples up to 20 assets and chooses the compatible portrait closest to the primary photo's capture time. With **±1 Day** or **±2 Days**, it broadens the search only when it cannot find a same-day companion. If those fast samples miss, Espframe pages through every eligible asset until it finds a compatible portrait, so large or shared albums do not produce false "no companion" results. The range is kept inside any date filter you have configured, and the companion uses the exact album, person, or tag chosen for the primary photo.

Turn on **Paired Portraits Only** to skip a portrait when a complete pair cannot be loaded. Landscape photos continue to display normally. While Espframe searches for another eligible photo, the last successfully displayed photo stays on screen.

---

## Metadata

Use **Metadata** in the **Immich** section of the web UI to control the photo information shown over the current image.

<!-- ESPFRAME:SETTINGS_TABLE metadata START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Location** | On | Shows the photo location when Immich has location data for the image. |
| **Date** | On | Shows the photo date. |
| **Date Format** | Date Taken | Choose whether the date uses the photo's taken date or a relative age. |
| **Date Taken Format** | `1 January, 2026` | Choose the display style used when **Date Format** is set to **Date Taken**. |
<!-- ESPFRAME:SETTINGS_TABLE metadata END -->

---

## Frequency

Use **Frequency** in the web UI to control slideshow timing and disconnect handling.

<!-- ESPFRAME:SETTINGS_TABLE frequency START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Slideshow Interval** | 15 seconds | How long each photo is shown before advancing (10 seconds to 24 hours). |
| **Connection Timeout** | 10 minutes | How long the frame waits without successfully displaying a new photo before showing the connection-failed screen (30 seconds to 30 minutes). |
<!-- ESPFRAME:SETTINGS_TABLE frequency END -->

Increase **Connection Timeout** if you have a slow server or large photo library and see false disconnects.

Before showing the connection-failed screen, Espframe retries temporary Immich errors. If Immich returns an API key error, the frame shows **Invalid API Key**; otherwise it shows **Unable to connect to Immich**.
