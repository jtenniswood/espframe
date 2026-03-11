# Immich Frame (ESPHome, Standalone)

*Last updated: 11 Mar 2026*

## Purpose

This document explains the implementation of a standalone digital photo frame powered by Immich, running on the Guition ESP32-P4 (JC8012P4A1) without Home Assistant. It is written to make future reimplementation easy.

## Project Structure

```
guition-esp32-p4-jc8012p4a1/
├── esphome.yaml                # User entry point (substitutions, wifi, remote package ref)
├── packages.yaml               # Aggregates all local packages
├── device/
│   └── device.yaml             # Hardware config (display, touch, SoC, external components)
├── addon/
│   ├── lvgl_base.yaml          # Shared LVGL config (buffer, displays)
│   ├── connectivity.yaml       # WiFi, captive portal, HTTP client
│   ├── backlight.yaml          # Display backlight light component
│   ├── screen_loading.yaml     # Loading screen (LVGL page + boot sequence)
│   ├── screen_wifi_setup.yaml  # WiFi setup screen (LVGL page)
│   ├── screen_slideshow.yaml   # Slideshow screen (LVGL page, ring buffer, API, scripts)
│   ├── accent_color.yaml       # Extract accent color from photo for letterbox fill
│   └── time.yaml               # SNTP clock and time label updates
├── assets/
│   ├── fonts.yaml              # Roboto font definitions (32/30/46/88/150pt)
│   └── icons.yaml              # MDI WiFi icon for setup screen
└── components/
    └── gsl3680/                # Custom touchscreen driver
components/
└── online_image/               # Patched online_image with header-based auth
```

Each screen is a self-contained YAML file that contributes an LVGL page, along with any screen-specific globals and scripts, to the merged config. ESPHome's `packages:` system deep-merges dictionaries and appends lists, so each file can independently define `lvgl: pages:`, `globals:`, `script:`, etc.

### User Setup

End users create a minimal `esphome.yaml` that references the remote package:

```yaml
substitutions:
  name: "immich-frame-10inch"
  friendly_name: "Immich Frame 10inch"
  immich_base_url: "http://192.168.1.30:2283"
  immich_api_key: !secret immich_api_key

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

packages:
  immich_frame:
    url: https://github.com/jtenniswood/immich-frame
    files: [guition-esp32-p4-jc8012p4a1/packages.yaml]
    ref: main
    refresh: 1s
```

## Architecture Overview

### 3-Slot Image Ring Buffer

Three `online_image` components (`immich_img_0`, `immich_img_1`, `immich_img_2`) form a circular buffer. At any time one slot is **active** (displayed), and the system prefetches into the next two slots. Each slot stores its own metadata (asset ID, image URL, date, location, year, month, person, is_portrait, datetime, companion_url, zoom).

When advancing forward, the active slot index moves `(active + 1) % 3`. If that slot is already prefetched, display is instant. Otherwise, a fresh API call fetches into it.

### Portrait Detection and Dual Display

When fetching an image, EXIF dimensions (`exifImageWidth`, `exifImageHeight`) and `orientation` are parsed to determine whether the photo is portrait. EXIF orientations 5–8 swap width and height before comparison. If `height > width`, the image is flagged as portrait.

When a portrait is displayed, the system searches Immich for a **companion portrait** taken on the same calendar day. If one is found, both portraits are downloaded at half-width (640×1200) and displayed **side-by-side** in a `portrait_pair_container`, filling the 1280×800 screen more effectively than a single letterboxed portrait.

The system uses four dedicated `online_image` components for portrait pairs:
- **`immich_portrait_left`** and **`immich_portrait_right`** — for the currently displayed pair.
- **`immich_portrait_preload_left`** and **`immich_portrait_preload_right`** — for prefetching the next portrait pair during the prefetch chain, enabling instant transitions.

If no companion is found or the companion download fails, the portrait falls back to single-image display in the standard slot.

### Landscape Zoom

For landscape images with aspect ratios between 1.6:1 and 2.0:1 (mildly panoramic), a zoom level is calculated to fill the 800px display height, slightly cropping the sides. The zoom value is stored per-slot as a `uint16_t` (256 = no zoom, higher = more zoom). LVGL's `lv_img_set_zoom` applies it at display time. Images wider than 2.0:1 are not zoomed (too panoramic to crop usefully).

### Previous Image Navigation

One previous image is stored separately (`immich_prev_*` globals, including zoom level). Swiping right swaps current and previous metadata, re-downloads the previous image URL into the newly active slot.

## Data Flow

### Forward Advance (timer, swipe left, or long press)

1. Save current metadata (including zoom) to `immich_prev_*` globals.
2. Reset portrait pair state (`portrait_left_ready`, `portrait_right_ready`, `portrait_companion_found`, `immich_is_portrait_pair`, `portrait_using_preload`).
3. Advance `active_slot` to `(active + 1) % 3`.
4. If next slot is already prefetched (`slotN_ready`), display instantly and kick off prefetch chain.
5. Otherwise, set `target_slot` and run `immich_fetch_into_slot`.

### Fetch Into Slot

1. `POST ${immich_base_url}/api/search/random` with `{"size":1,"type":"IMAGE","withExif":true,"withPeople":true}`.
2. Parse response for asset `id`, `localDateTime`, `exifInfo` (city, country, dateTimeOriginal, exifImageWidth, exifImageHeight, orientation), and `people` (first person name).
3. Determine portrait status from EXIF dimensions (accounting for orientation tags 5–8 which swap axes). Calculate zoom for mildly panoramic landscapes.
4. Build image URL: `${immich_base_url}/api/assets/{id}/thumbnail?size=preview`.
5. Store parsed metadata (including `is_portrait`, `datetime`, `companion_url`, `zoom`) into the target slot's globals.
6. Trigger `online_image.set_url` on the corresponding image component.

### Image Download Callback

1. `on_download_finished` marks slot as ready, resets error retries.
2. If this slot is the active slot, copy its metadata to the current display globals and run `immich_display_current`.
3. If this slot is **not** the active slot and is portrait, trigger `immich_fetch_portrait_companion` to search for a companion portrait and begin preloading the pair.
4. Otherwise, trigger `immich_prefetch_chain` to fill the next empty slot(s).

### Display Current (`immich_display_current`)

1. Show the standard `slideshow_img` and hide the `portrait_pair_container`.
2. Update `slideshow_img` source to the active slot's image and apply the slot's zoom level.
3. Update overlay labels (location, time ago).
4. Run accent color extraction.
5. **Portrait preload check:** If the active slot is portrait and portrait preload data is ready for this slot, instantly switch to portrait pair display using the preloaded images.
6. **Portrait companion check:** If the active slot is portrait but no preloaded pair is available, start downloading the left (primary) portrait and either use the already-known companion URL or trigger `immich_fetch_portrait_companion` to find one.

### Companion Portrait Search (`immich_fetch_portrait_companion`)

1. Extract the calendar date (first 10 characters) from the portrait's `localDateTime`.
2. `POST ${immich_base_url}/api/search/random` with `{"size":10,"type":"IMAGE","withExif":true,"takenAfter":"YYYY-MM-DDT00:00:00.000Z","takenBefore":"YYYY-MM-DDT23:59:59.999Z"}`.
3. Iterate results, skip the primary asset, and check EXIF dimensions for portrait orientation.
4. If a companion is found:
   - If the target slot is the **active** slot, start downloading into `immich_portrait_right` immediately.
   - If the target slot is a **prefetch** slot, start preloading both halves into `immich_portrait_preload_left` and `immich_portrait_preload_right`.
5. Store the companion URL in the slot's `companion_url` global so it's available for instant use when that slot becomes active.
6. Continue the prefetch chain regardless of companion search outcome.

### Display Portrait Pair (`immich_display_portrait_pair`)

1. Called when both `portrait_left_ready` and `portrait_right_ready` are true.
2. Hide `slideshow_img`, show `portrait_pair_container`.
3. Update `portrait_left_img` and `portrait_right_img` sources.
4. Update overlay labels and run accent color extraction.
5. Continue the prefetch chain.

### Prefetch Chain

Checks the two slots ahead of `active_slot`. Fetches into the first one that isn't ready. Runs recursively via the download callback until all slots are filled.

## Immich API Contract

- **Random image selection:**
  - `POST ${immich_base_url}/api/search/random`
  - Headers: `Content-Type: application/json`, `Accept: application/json`, `x-api-key: ${immich_api_key}`
  - Body: `{"size":1,"type":"IMAGE","withExif":true,"withPeople":true}`

- **Companion portrait search (same-day portraits):**
  - `POST ${immich_base_url}/api/search/random`
  - Headers: `Content-Type: application/json`, `Accept: application/json`, `x-api-key: ${immich_api_key}`
  - Body: `{"size":10,"type":"IMAGE","withExif":true,"takenAfter":"YYYY-MM-DDT00:00:00.000Z","takenBefore":"YYYY-MM-DDT23:59:59.999Z"}`
  - Response is filtered client-side: skip the primary asset, check EXIF for portrait orientation, use the first match.

- **Rendered image bytes:**
  - `GET ${immich_base_url}/api/assets/{id}/thumbnail?size=preview`
  - Header: `x-api-key: ${immich_api_key}` (configured on the `online_image` component via `request_headers`)

## Config Inputs and Secrets

Substitutions provided by the user in `esphome.yaml`:

| Substitution | Example | Description |
|---|---|---|
| `name` | `immich-frame-10inch` | ESPHome device name |
| `friendly_name` | `Immich Frame 10inch` | Human-readable name |
| `immich_base_url` | `http://192.168.1.30:2283` | Immich server URL |
| `immich_api_key` | `!secret immich_api_key` | Immich API key |

Secrets in `secrets.yaml`:

| Secret | Description |
|---|---|
| `wifi_ssid` | WiFi network name |
| `wifi_password` | WiFi password |
| `immich_api_key` | Immich API key |

Tunable defaults:

| Substitution | Default | Defined in | Description |
|---|---|---|---|
| `immich_slide_interval_seconds` | `15` | `addon/screen_slideshow.yaml` | Default slideshow interval in seconds (runtime-adjustable via HA) |
| `immich_verify_ssl` | `false` | `addon/connectivity.yaml` | TLS certificate verification |

## UI Layout

The UI uses separate LVGL pages for each screen, navigated via `lvgl.page.show`. The first page in the merged config (`loading_page`) is shown at startup.

### `loading_page` (defined in `addon/screen_loading.yaml`)

"Starting up" label and a progress bar. Shown at boot; the system transitions away once WiFi connects or the 10s grace period expires.

### `wifi_setup_page` (defined in `addon/screen_wifi_setup.yaml`)

WiFi icon and instructions to connect to the captive portal hotspot. Shown via `lvgl.page.show: wifi_setup_page` when WiFi disconnects after the boot grace period.

### `slideshow_page` (defined in `addon/screen_slideshow.yaml`)

The main photo display. Shown via `lvgl.page.show: slideshow_page` when WiFi connects.

- **`slideshow_img`** — Full-screen LVGL image widget displaying the active slot's image. Zoom level is applied per-slot via `lv_img_set_zoom`. Hidden when a portrait pair is active.
- **`portrait_pair_container`** — Hidden by default. A 1280×800 flex ROW container with two 640×800 child objects, each containing an image widget (`portrait_left_img`, `portrait_right_img`). Shown when a portrait pair is displayed; supports the same touch gestures as `slideshow_img`. Each portrait image is resized to 640×1200 and centred within its half.
- **`info_overlay`** — Transparent container anchored to bottom-left (no background bar). Tap image to toggle visibility. Uses a flex ROW layout with the clock on the left and metadata stacked vertically to its right.
  - **`time_label`** — Current time (HH:MM) in Roboto Light 150px, updated every 60s via SNTP.
  - **`time_ago_label`** — Relative photo age ("3 years ago") in Roboto Light 46px.
  - **`location_label`** — Photo location (city, country) in Roboto Light 32px.
  - Each label has a paired `*_shadow` label rendered behind it (same text, black at 50% opacity, offset 2px right and 2px down) for readability over photos.

### Touch Gestures

| Gesture | Threshold | Action |
|---|---|---|
| Swipe left | dx < -80px | Advance to next image |
| Swipe right | dx > 80px | Show previous image |
| Tap | \|dx\| <= 80px | Toggle info overlay |
| Long press | -- | Advance to next image |

Both `slideshow_img` and `portrait_pair_container` handle these gestures identically.

### Adding a New Screen

To add a screen, create a single `addon/screen_*.yaml` file and add one `!include` line to `packages.yaml`. No existing files need modification.

The new file defines its own LVGL page plus any screen-specific globals and scripts:

```yaml
# addon/screen_settings.yaml
lvgl:
  pages:
    - id: settings_page
      bg_color: 0x000000
      scrollable: false
      widgets:
        # ... screen widgets

globals:
  - id: my_screen_state
    type: int
    initial_value: '0'

script:
  - id: my_screen_script
    then:
      # ...
```

Then register it in `packages.yaml`:

```yaml
  screen_settings: !include addon/screen_settings.yaml
```

Navigate to the screen from any file using `lvgl.page.show: settings_page`. Shared resources (fonts, icons, time, accent color) are referenced by their global IDs across files.

## Boot Sequence

1. **Priority -200:** Turn on backlight at 100%, set progress bar to 25%.
2. **Priority -100:** Set progress bar to 50%. Populate WiFi setup instructions with device name.
3. **WiFi connect:** Set progress bar to 100%, navigate to `slideshow_page`, start first image fetch.
4. **10s timeout:** End boot grace period. If still no WiFi, navigate to `wifi_setup_page`.

## Runtime Behavior and Guardrails

- **Advance debounce:** Timer skips if last advance was less than 10s ago.
- **In-flight guard:** All scripts use `mode: single` to prevent overlapping API calls.
- **Error retry:** Up to 3 retries with 2s delay on image decode failure, then gives up and resets counter.
- **WiFi disconnect:** Shows captive portal setup prompt (after boot grace period).
- **Slot readiness:** Forward advance blocks display until the slot's image has fully downloaded; current image remains on screen until then.
- **Portrait fallback:** If portrait companion search fails or either portrait image fails to decode, the system falls back to single-image display.
- **Portrait state reset:** On every advance (forward or backward), all portrait pair state is reset to prevent stale data.

## Accent Color

The `extract_accent_color` script (in `addon/accent_color.yaml`) samples the displayed image and derives a dominant colour for letter-boxed areas, so they complement the photo instead of staying plain black.

### How It Works

1. The sampling logic is encapsulated in a reusable `fill_accent` lambda that operates on any `esphome::image::Image*`.
2. A 20×20 grid of pixels is sampled from the image's RGB565 buffer (read little-endian to match the display byte order), skipping letterbox padding by scanning inward from the edges for the first non-zero pixel.
3. Each pixel's saturation (max channel − min channel) is computed. The weight is `sat² + 1`, so vivid colours dominate while blacks, whites, and greys contribute minimally.
4. A weighted average produces the accent RGB. This is darkened to half intensity (`r/2, g/2, b/2`) so the background doesn't overpower the photo.
5. The darkened colour is written directly into the letterbox pixels of the raw image buffer.
6. For **portrait pairs**, `fill_accent` is called on both the left and right portrait images (or their preloaded equivalents), and the `portrait_pair_container` is invalidated. For **single images**, it is called on the active slot's image and `slideshow_img` is invalidated.

### LVGL Background Color Caveat

When setting an LVGL object's background colour from C/lambda code, you must set **both** the colour and the opacity. Setting only `lv_obj_set_style_bg_color` has no visible effect because the background opacity defaults to transparent. Always pair it with `lv_obj_set_style_bg_opa`:

```c
lv_obj_set_style_bg_color(id(slideshow_page)->obj, color, 0);
lv_obj_set_style_bg_opa(id(slideshow_page)->obj, LV_OPA_COVER, 0);
```

Also note that ESPHome's `LvPageType*` is not a raw LVGL object pointer — use `->obj` to get the underlying `_lv_obj_t*` that LVGL functions expect.

## External Components

- **`online_image`** (patched, from `components/online_image/`): Adds `request_headers` support so image fetches use `x-api-key` header auth instead of query-string keys. Includes `libjpeg-turbo-esp32` for hardware-accelerated JPEG decoding.
- **`gsl3680`** (from `guition-esp32-p4-jc8012p4a1/components/gsl3680/`): Custom touchscreen driver for the GSL3680 controller on this panel.

## Known Constraints

- Only one previous image is stored; you cannot swipe back more than once.
- `immich_verify_ssl: false` is convenient for self-signed cert setups but not ideal long-term.
- Standard landscape images are resized to 1280×960. Portrait images for pair display are resized to 640×1200 each.
- SNTP timezone is hardcoded to `Europe/London` in `addon/time.yaml`.
- Companion portrait search fetches up to 10 random same-day images and uses the first portrait match; it does not guarantee the best pairing.
- Images wider than 2.0:1 aspect ratio are not zoomed (displayed with letterboxing).

## Troubleshooting

- **`HTTP 401 from Immich`**: API key invalid or lacks permission.
- **`No valid asset in response`**: Response shape changed, or no matching assets in Immich.
- **Image does not update but asset ID logs change**: Image endpoint auth issue, SSL failure, or JPEG decode error.
- **`Slot N decode failed`**: Image too large or corrupt. Will retry up to 3 times then skip.
- **WiFi setup screen appears**: Device lost WiFi. Connect to the captive portal hotspot shown on screen.
- **Timer says "skipped, last advance Xms ago"**: A manual swipe happened recently; timer will resume after the debounce window.
- **`Portrait left/right decode failed, falling back to single`**: One of the portrait pair images failed to download or decode. The system falls back to single-image display.
- **`No companion portrait found for this day`**: No other portrait photo exists on the same calendar day in Immich. The portrait is shown alone.

## Future Improvements

1. Make SNTP timezone configurable via substitution.
2. Add album/favorites/people/date-range filters to the `/search/random` request body.
3. Support deeper backward navigation history (ring buffer of previous URLs).
4. Add screen dimming schedule or ambient light sensor integration.
5. Smarter companion portrait matching (prefer similar location, people, or time-of-day).
6. Define shared LVGL `style_definitions` in `addon/lvgl_base.yaml` for consistent styling across screens.
7. Add page transition animations via `lvgl.page.show` options (`animation`, `time`).
