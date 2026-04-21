# espframe component

ESPHome component that provides shared C++ helpers for the Espframe-for-Immich digital photo frame: date formatting, Immich API parsing, sunrise/sunset calculation, and slot/display metadata types.

## Requirements

- **ESPHome** with the **json** component enabled.
- Include this component in your configuration (e.g. via `external_components` or packages) and add `espframe:` to your YAML so the component’s headers are on the include path.

## Files

| File | Purpose |
|------|--------|
| `espframe_helpers.h` | Main entry: data types (`PhotoMeta`, `SlotMeta`, `DisplayMeta`), copy helpers, and `parse_immich_asset_and_fill_slot`. Include this from YAML lambdas when you need slot/display types or Immich parsing. |
| `date_utils.h` | Month names, URL normalization, and human‑readable date/time-ago formatting. |
| `immich_helpers.h` | Immich API: search body builder, UUID array builder, and JSON asset parser filling `ImmichAssetMeta`. |
| `sun_calc.h` | Timezone coordinate lookup and NOAA-based sunrise/sunset calculation. |

## Constants

Defined in the headers:

| Constant | Header | Value / meaning |
|----------|--------|------------------|
| `MAX_ERROR_RETRIES` | `espframe_helpers.h` | `3` — suggested max retries for Immich API or download failures. |
| `ACCENT_GRID_SIZE` | `espframe_helpers.h` | `20` — grid size used for sampling accent colour from images. |
| `ZOOM_IDENTITY` | `immich_helpers.h` | `256` — no zoom (1:1). |
| `MONTH_NAMES` | `date_utils.h` | `""`, `"Jan"` … `"Dec"` — short month names (index 0 unused). |
| `TZ_DATA` | `sun_calc.h` | Array of `TzInfo` (tz id, lat, lon, POSIX TZ string) for many IANA timezones. |
| `TZ_DATA_COUNT` | `sun_calc.h` | Number of entries in `TZ_DATA`. |
| `DEG_TO_RAD`, `RAD_TO_DEG` | `sun_calc.h` | Angle conversion constants. |

---

## Data types

### `PhotoMeta` (espframe_helpers.h)

Base metadata for a single photo.

| Field | Type | Meaning |
|-------|------|--------|
| `asset_id` | `std::string` | Immich asset ID. |
| `image_url` | `std::string` | Full URL for thumbnail/preview (e.g. `/api/assets/.../thumbnail?size=preview`). |
| `date` | `std::string` | Human-readable date (e.g. `"10 October 2022"` when a full date is known). |
| `location` | `std::string` | Location string (e.g. city, country from EXIF). |
| `person` | `std::string` | First person name from Immich people. |
| `year`, `month`, `day` | `int` | Photo date parts when known. |
| `zoom` | `uint16_t` | Display zoom (e.g. `ZOOM_IDENTITY` = no zoom). |

### `SlotMeta` (espframe_helpers.h)

Extends `PhotoMeta` for a preload slot (one of three buffers).

| Additional field | Type | Meaning |
|-------------------|------|--------|
| `datetime` | `std::string` | Raw local date/time from asset (e.g. for “on this day”). |
| `companion_url` | `std::string` | URL for portrait companion image when pairing. |
| `pending_asset_id` | `std::string` | Asset ID currently being loaded. |
| `ready` | `bool` | Whether the slot has a loaded image ready to show. |
| `is_portrait` | `bool` | Whether the image is portrait orientation. |

### `DisplayMeta` (espframe_helpers.h)

Extends `PhotoMeta` for the currently displayed photo.

| Additional field | Type | Meaning |
|------------------|------|--------|
| `valid` | `bool` | Whether the display is showing a valid photo. |

### `ImmichAssetMeta` (immich_helpers.h)

Filled by `parse_immich_asset` from Immich JSON. Same logical fields as `PhotoMeta` plus `datetime` and `is_portrait` (see `immich_helpers.h`).

### `TzInfo` (sun_calc.h)

| Field | Type | Meaning |
|-------|------|--------|
| `tz` | `const char*` | IANA timezone identifier. |
| `lat`, `lon` | `float` | Representative latitude/longitude (degrees). |
| `posix` | `const char*` | POSIX TZ string for ESPHome's `set_timezone()`. |

---

## Functions

### espframe_helpers.h

#### `copy_slot_to_display(const SlotMeta &slot, DisplayMeta &disp)`

Copies the `PhotoMeta` part of `slot` into `disp`. Slot-only fields (`datetime`, `companion_url`, etc.) are not copied.

**Use when:** Moving from a preload slot to “current display” state.

```cpp
copy_slot_to_display(id(slot0), id(current_display));
id(current_display).valid = true;
```

#### `copy_display_to_slot(const DisplayMeta &disp, SlotMeta &slot)`

Copies the `PhotoMeta` part of `disp` into `slot`. Display-only field `valid` is not copied.

**Use when:** Copying the currently displayed photo back into a slot (e.g. for “previous” or comparison).

```cpp
copy_display_to_slot(id(current_display), id(slot0));
```

#### `parse_immich_asset_and_fill_slot(body, base_url, slot, s0, s1, s2, orientation_filter)`
**Signature:**
`std::string parse_immich_asset_and_fill_slot(const std::string &body, const std::string &base_url, int slot, SlotMeta &s0, SlotMeta &s1, SlotMeta &s2, const std::string &orientation_filter = "Any")`

Parses Immich JSON (single asset object or array), fills the corresponding slot (`slot` 0, 1, or 2), and returns the image URL. When `orientation_filter` is `"Portrait Only"` or `"Landscape Only"`, arrays are searched for the first matching asset. Returns empty string on parse failure or no matching asset. Requires `USE_JSON` (ESPHome json component).

- **body** — JSON response body (e.g. from `/api/assets/{id}` or `/api/search/random`).
- **base_url** — Immich base URL with no trailing slash (e.g. `id(immich_url).state`).
- **slot** — Which slot to fill: 0, 1, or 2.
- **s0, s1, s2** — References to your three `SlotMeta` globals (e.g. `id(slot0)`, `id(slot1)`, `id(slot2)`).
- **orientation_filter** — Optional: `"Any"`, `"Portrait Only"`, or `"Landscape Only"`.

**Use when:** Handling Immich API responses in HTTP request lambdas.

```cpp
std::string img_url = parse_immich_asset_and_fill_slot(body, id(immich_url).state, id(target_slot), id(slot0), id(slot1), id(slot2));
if (!img_url.empty()) {
  // use img_url to load image; slot meta is already filled
}
```

---

### date_utils.h

#### `strip_trailing_slashes(const std::string &url)`

Returns a copy of `url` with trailing `'/'` characters removed.

**Use when:** Normalizing Immich or other base URLs before concatenating paths.

```cpp
std::string base = strip_trailing_slashes(id(immich_url).state);
```

#### `format_time_ago(photo_year, photo_month, now_year, now_month)`

Returns a short relative date string: `"N months ago"`, `"1 year ago"`, `"N years ago"`, or `""` if the photo is from this month or invalid.

**Use when:** Showing “time ago” in the UI.

```cpp
std::string ago = format_time_ago(meta.year, meta.month, id(now_year), id(now_month));
```

#### `format_photo_age(photo_year, photo_month, photo_day, now_year, now_month, now_day)`

Returns a short relative photo age such as `"today"`, `"12 days ago"`, `"3 months ago"`, or `"4 years ago"`. Uses days as the smallest unit, then rounds to months or years for older photos.

**Use when:** Showing relative photo time metadata in the slideshow UI.

#### `format_photo_date(year, month)`

Returns a string like `"Jan 2024"` for valid month (1–12), otherwise `""`. Uses `MONTH_NAMES`.

**Use when:** Displaying a photo’s date.

```cpp
std::string date_str = format_photo_date(meta.year, meta.month);
```

#### `format_photo_date_full(year, month, day)`

Returns a full display date such as `"10 October 2022"` or `""` if the date parts are invalid.

---

### immich_helpers.h

#### `split_uuid_csv(const std::string &csv)`

Parses a comma-separated UUID list (optional spaces) into a vector of trimmed tokens; empty segments are skipped.

#### `pick_one_person_id_for_random_search(const std::string &csv)`

Returns one UUID from the list. Immich’s API applies **AND** when multiple `personIds` are sent; espframe picks a **random** person per `POST /api/search/random` so the slideshow covers **any** of the listed people over time.

#### `build_uuid_json_array(const std::string &csv)`

Turns a comma-separated list of UUIDs (with optional spaces) into a JSON array string, e.g. `"id1, id2"` → `["id1","id2"]`.

**Use when:** Building request bodies that need `albumIds` or `personIds` arrays.

```cpp
std::string album_ids_json = build_uuid_json_array(id(album_ids_text).state);
```

#### `build_immich_search_body(size, with_people, photo_source, album_ids, person_ids, extra)`

Builds the JSON body for Immich `POST /api/search/random`.  
**Parameters:** `size` (requested count), `with_people` (include people in response), `photo_source` (e.g. `"Favorites"`, `"Album"`, `"Person"`), `album_ids` / `person_ids` (CSV UUIDs for Album/Person), optional `extra` JSON fragment (e.g. `"\"takenAfter\":\"2024-01-01\""`). For **`Person`**, multiple IDs in `person_ids` are resolved to **one random ID per request** (any-of behavior vs Immich’s multi-ID AND).

**Use when:** Building the body for random or “on this day” search requests.

```cpp
std::string body = build_immich_search_body(1, true, id(photo_source_select).current_option(),
  id(album_ids_text).state, id(person_ids_text).state);
// with optional extra for date filter:
std::string body_memories = build_immich_search_body(1, true, "All", "", "", "\"takenAfter\":\"2024-01-01\",\"takenBefore\":\"2024-01-02\"");
```

#### `resolve_immich_date_filter(...)`

Converts the configured fixed or relative date filter into normalized `YYYY-MM-DD` bounds. `build_immich_date_filter_extra(range)` turns those bounds into Immich `takenAfter` / `takenBefore` JSON fields, while `build_immich_companion_date_filter_extra(day, range)` clips portrait companion searches to the target day.

**Use when:** Building Immich search request bodies that must honor the same date-filter rules.

#### `parse_immich_asset(body, base_url, out_meta, orientation_filter)`
**Signature:**
`std::string parse_immich_asset(const std::string &body, const std::string &base_url, ImmichAssetMeta *out_meta, const std::string &orientation_filter = "Any")`

Parses Immich asset JSON (single object or array), fills `out_meta`, and returns the thumbnail URL. With an orientation filter, array responses are searched for the first matching photo. Returns `""` on failure or no matching asset. Requires `USE_JSON`.
Use `parse_immich_asset_and_fill_slot` in espframe_helpers.h if you want to fill a `SlotMeta` directly.

---

### sun_calc.h

#### `lookup_tz_coords(tz_id, lat, lon)`

Looks up latitude and longitude for an IANA timezone string (e.g. `"America/New_York"`). Returns `true` and sets `lat`/`lon` if found, otherwise `false`.

**Use when:** You need coordinates for sunrise/sunset for a selected timezone.

```cpp
float lat, lon;
if (lookup_tz_coords(id(timezone_select).current_option(), lat, lon)) {
  // use lat, lon with calc_sunrise_sunset
}
```

#### `lookup_tz_posix(tz_id)`

Returns the POSIX TZ string for an IANA timezone identifier (e.g. `"America/Denver"` → `"MST7MDT,M3.2.0,M11.1.0"`). Returns `nullptr` if not found.

**Use when:** Setting the timezone at runtime via `set_timezone()`, which requires POSIX format.

```cpp
const char* posix = lookup_tz_posix("America/Denver");
if (posix) {
  id(sntp_time).set_timezone(posix);
}
```

#### `calc_sunrise_sunset(year, month, day, lat, lon, tz_offset, rise_h, rise_m, set_h, set_m)`

Computes sunrise and sunset for the given date and location. Outputs are local time: `rise_h`/`rise_m`, `set_h`/`set_m`. `tz_offset` is in hours (e.g. from `parse_tz_offset`). Returns `false` for polar day/night; fallback values 06:00 and 18:00 are still written.

**Use when:** Driving backlight or “screen on” schedules from sunrise/sunset.

```cpp
int rise_h, rise_m, set_h, set_m;
if (calc_sunrise_sunset(year, month, day, lat, lon, tz_offset, rise_h, rise_m, set_h, set_m)) {
  // use rise_h:rise_m and set_h:set_m
}
```

#### `parse_tz_offset(tz_label)`

Parses a timezone label string that contains `"GMT"` and an optional offset (e.g. `"GMT-5"`, `"GMT+5:30"`). Returns the offset in hours (positive = east). Returns `0.0f` if not found or invalid.

**Use when:** Converting a UI timezone label to an hour offset for `calc_sunrise_sunset`.

```cpp
float tz_offset = parse_tz_offset(id(timezone_select).current_option());
```

---

## How to use in ESPHome YAML

1. Add the espframe component (e.g. from this repo) and include it in `external_components` / `packages` so that `components: [..., espframe]` is used and `espframe:` appears in your config.
2. In lambdas that need slot/display types or Immich parsing, the build will have the component’s directory on the include path; include the main header:

   ```yaml
   lambda: |-
     #include "espframe_helpers.h"
     auto &meta = id(slot0);
     std::string img_url = parse_immich_asset_and_fill_slot(body, id(immich_url).state, id(target_slot), id(slot0), id(slot1), id(slot2));
   ```

3. Define globals with the component’s types (e.g. `SlotMeta`, `DisplayMeta`) and pass them by reference into the functions above. See the addons in this repo (e.g. `immich_api.yaml`, `immich_slideshow.yaml`) for full examples.

## License

See [LICENSE](LICENSE) in this directory.
