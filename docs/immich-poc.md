# Immich Frame (ESPHome, Standalone)

*Last updated: 14 Dec 2025*

## Purpose

This document explains the implementation of a standalone digital photo frame powered by Immich, running on the Guition ESP32-P4 (JC8012P4A1) without Home Assistant. It is written to make future reimplementation easy.

## Project Structure

```
guition-esp32-p4-jc8012p4a1/
├── esphome.yaml              # User entry point (substitutions, wifi, remote package ref)
├── packages.yaml             # Aggregates all local packages
├── device/
│   └── device.yaml           # Hardware config (display, touch, SoC, external components)
├── addon/
│   ├── immich.yaml           # Slideshow logic, UI, ring buffer, API calls
│   ├── accent_color.yaml     # Extract accent color from photo for page background
│   └── time.yaml             # SNTP clock and time label updates
├── assets/
│   ├── fonts.yaml            # Roboto font definitions (26/30/40/88pt)
│   └── icons.yaml            # MDI WiFi icon for setup screen
└── components/
    └── gsl3680/              # Custom touchscreen driver
components/
└── online_image/             # Patched online_image with header-based auth
```

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

Three `online_image` components (`immich_img_0`, `immich_img_1`, `immich_img_2`) form a circular buffer. At any time one slot is **active** (displayed), and the system prefetches into the next two slots. Each slot stores its own metadata (asset ID, image URL, date, location, year, month, person).

When advancing forward, the active slot index moves `(active + 1) % 3`. If that slot is already prefetched, display is instant. Otherwise, a fresh API call fetches into it.

### Previous Image Navigation

One previous image is stored separately (`immich_prev_*` globals). Swiping right swaps current and previous metadata, re-downloads the previous image URL into the newly active slot.

## Data Flow

### Forward Advance (timer, swipe left, or long press)

1. Save current metadata to `immich_prev_*` globals.
2. Advance `active_slot` to `(active + 1) % 3`.
3. If next slot is already prefetched (`slotN_ready`), display instantly and kick off prefetch chain.
4. Otherwise, set `target_slot` and run `immich_fetch_into_slot`.

### Fetch Into Slot

1. `POST ${immich_base_url}/api/search/random` with `{"size":1,"type":"IMAGE","withExif":true,"withPeople":true}`.
2. Parse response for asset `id`, `localDateTime`, `exifInfo` (city, country, dateTimeOriginal), and `people` (first person name).
3. Build image URL: `${immich_base_url}/api/assets/{id}/thumbnail?size=preview` (no query key).
4. Store parsed metadata into the target slot's globals.
5. Trigger `online_image.set_url` on the corresponding image component.

### Image Download Callback

1. `on_download_finished` marks slot as ready, resets error retries.
2. If this slot is the active slot, copy its metadata to the current display globals and run `immich_display_current`.
3. Trigger `immich_prefetch_chain` to fill the next empty slot(s).

### Prefetch Chain

Checks the two slots ahead of `active_slot`. Fetches into the first one that isn't ready. Runs recursively via the download callback until all slots are filled.

## Immich API Contract

- **Random image selection:**
  - `POST ${immich_base_url}/api/search/random`
  - Headers: `Content-Type: application/json`, `Accept: application/json`, `x-api-key: ${immich_api_key}`
  - Body: `{"size":1,"type":"IMAGE","withExif":true,"withPeople":true}`

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

Tunable defaults in `addon/immich.yaml`:

| Substitution | Default | Description |
|---|---|---|
| `immich_slide_interval` | `15s` | Auto-advance interval |
| `immich_verify_ssl` | `false` | TLS certificate verification |

## UI Layout

### Main Page

- **`slideshow_img`** -- Full-screen LVGL image widget displaying the active slot's image.
- **`info_overlay`** -- Bottom bar (56px, 50% opacity black). Tap image to toggle visibility.
  - Left: **`time_label`** -- Current time (HH:MM), updated every 60s via SNTP.
  - Right: **`meta_label`** -- Photo metadata: date, relative age ("3 years ago"), person name, location (city, country). Fields separated by bullet characters.
- **`loading_screen`** -- Shown on boot with "Starting up" text and a progress bar. Hidden after WiFi connects or 10s grace period.
- **`wifi_setup_prompt`** -- WiFi icon + instructions to connect to the captive portal hotspot. Shown when WiFi disconnects after boot grace period.

### Touch Gestures

| Gesture | Threshold | Action |
|---|---|---|
| Swipe left | dx < -80px | Advance to next image |
| Swipe right | dx > 80px | Show previous image |
| Tap | \|dx\| <= 80px | Toggle info overlay |
| Long press | -- | Advance to next image |

## Boot Sequence

1. **Priority -200:** Turn on backlight at 100%, set progress bar to 25%.
2. **Priority -100:** Set progress bar to 50%. Populate WiFi setup instructions with device name.
3. **WiFi connect:** Set progress bar to 100%, hide loading screen, start first image fetch.
4. **10s timeout:** End boot grace period. If still no WiFi, hide loading screen and show WiFi setup prompt.

## Runtime Behavior and Guardrails

- **Advance debounce:** Timer skips if last advance was less than 10s ago.
- **In-flight guard:** All scripts use `mode: single` to prevent overlapping API calls.
- **Error retry:** Up to 3 retries with 2s delay on image decode failure, then gives up and resets counter.
- **WiFi disconnect:** Shows captive portal setup prompt (after boot grace period).
- **Slot readiness:** Forward advance blocks display until the slot's image has fully downloaded; current image remains on screen until then.

## Accent Color

The `extract_accent_color` script (in `addon/accent_color.yaml`) samples the displayed image and derives a dominant colour for the `main_page` background, so letter-boxed areas complement the photo instead of staying plain black.

### How It Works

1. A 20×20 grid of pixels is sampled from the active slot's raw RGB565 buffer (read little-endian to match the display byte order).
2. Each pixel's saturation (max channel − min channel) is computed. The weight is `sat² + 1`, so vivid colours dominate while blacks, whites, and greys contribute minimally.
3. A weighted average produces the accent RGB. This is darkened to one-third intensity (`r/3, g/3, b/3`) so the background doesn't overpower the photo.
4. The darkened colour is applied to the LVGL page at runtime.

### LVGL Background Color Caveat

When setting an LVGL object's background colour from C/lambda code, you must set **both** the colour and the opacity. Setting only `lv_obj_set_style_bg_color` has no visible effect because the background opacity defaults to transparent. Always pair it with `lv_obj_set_style_bg_opa`:

```c
lv_obj_set_style_bg_color(id(main_page)->obj, color, 0);
lv_obj_set_style_bg_opa(id(main_page)->obj, LV_OPA_COVER, 0);
```

Also note that ESPHome's `LvPageType*` is not a raw LVGL object pointer — use `->obj` to get the underlying `_lv_obj_t*` that LVGL functions expect.

## External Components

- **`online_image`** (patched, from `components/online_image/`): Adds `request_headers` support so image fetches use `x-api-key` header auth instead of query-string keys. Includes `libjpeg-turbo-esp32` for hardware-accelerated JPEG decoding.
- **`gsl3680`** (from `guition-esp32-p4-jc8012p4a1/components/gsl3680/`): Custom touchscreen driver for the GSL3680 controller on this panel.

## Known Constraints

- Only one previous image is stored; you cannot swipe back more than once.
- `immich_verify_ssl: false` is convenient for self-signed cert setups but not ideal long-term.
- Image resize is fixed at 1280x960 (landscape). Portrait photos will be letter-boxed.
- SNTP timezone is hardcoded to `Europe/London` in `addon/time.yaml`.

## Troubleshooting

- **`HTTP 401 from Immich`**: API key invalid or lacks permission.
- **`No valid asset in response`**: Response shape changed, or no matching assets in Immich.
- **Image does not update but asset ID logs change**: Image endpoint auth issue, SSL failure, or JPEG decode error.
- **`Slot N decode failed`**: Image too large or corrupt. Will retry up to 3 times then skip.
- **WiFi setup screen appears**: Device lost WiFi. Connect to the captive portal hotspot shown on screen.
- **Timer says "skipped, last advance Xms ago"**: A manual swipe happened recently; timer will resume after the debounce window.

## Future Improvements

1. Make SNTP timezone configurable via substitution.
2. Add album/favorites/people/date-range filters to the `/search/random` request body.
3. Support deeper backward navigation history (ring buffer of previous URLs).
4. Add screen dimming schedule or ambient light sensor integration.
5. Configurable image resize dimensions for portrait vs landscape orientation.
