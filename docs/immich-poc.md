# Immich Slideshow POC (ESPHome, No Home Assistant)

## Purpose

This document explains the proof-of-concept implementation that displays random Immich photos directly on the ESP32-P4 dashboard, without requiring Home Assistant as a data source.

It is written to make future reimplementation easy.

## What Was Added

- Local package include in `guition-esp32-p4-jc8012p4a1/packages.yaml`:
  - `immich_poc: !include addon/immich_poc.yaml`
- New local package file:
  - `guition-esp32-p4-jc8012p4a1/addon/immich_poc.yaml`

The upstream remote package remains untouched. All POC behavior is layered locally.

## POC Data Flow

1. Interval triggers `immich_request_next_image`.
2. Device sends `POST /api/search/random` to Immich.
3. Response body is parsed for the first asset `id`.
4. Device builds thumbnail URL:
   - `/api/assets/{id}/thumbnail?size=preview&key=...`
5. Device updates the existing image component with `online_image.set_url`.
6. Existing LVGL image widget displays the new image.

## Immich API Contract Used

- Random image selection:
  - `POST ${immich_base_url}/api/search/random`
  - Headers:
    - `Content-Type: application/json`
    - `Accept: application/json`
    - `x-api-key: ${immich_api_key}`
  - Body:
    - `{"size":1,"type":"IMAGE"}`
- Rendered image bytes:
  - `GET ${immich_base_url}/api/assets/{id}/thumbnail?size=preview&key=${immich_api_key}`

## Config Inputs and Secrets

The POC expects these secrets:

- `immich_base_url` (example: `http://immich.local:2283`)
- `immich_api_key`

Other POC knobs in `addon/immich_poc.yaml`:

- `immich_slide_interval` (default `45s`)
- `immich_verify_ssl` (default `false` for easier POC bring-up)
- `immich_error_backoff_cycles` (default `2`)

## Runtime Behavior and Guardrails

- In-flight guard prevents overlapping API requests.
- Backoff cycles reduce retry spam after failures.
- If parsing fails or response is not 200, current image remains on screen.
- POC hides HA onboarding overlays periodically so no-HA operation stays visible.

## Known Constraints

- Thumbnail URL currently uses query `key=` for image fetch compatibility with existing `online_image` config.
- `immich_verify_ssl: false` is convenient for self-signed cert setups but not ideal long-term.
- Existing music controls/widgets still exist in the UI, but slideshow now drives artwork and title text.

## Troubleshooting

- `search/random failed: status=401`:
  - API key invalid or lacks permission.
- `No asset id in response body`:
  - Response shape changed, or no matching assets.
- Image does not update but ID logs change:
  - Thumbnail endpoint auth issue, or SSL failure.
- Frequent request errors:
  - Check network reachability and TLS mode (`immich_verify_ssl`).

## Clean Final-Form Recommendations

For the final implementation, consider:

1. Replace query-key thumbnail URL with header-based auth for image fetch (dedicated image component config).
2. Build a dedicated slideshow page (instead of reusing music labels and overlays).
3. Remove periodic prompt-hiding workaround by introducing an explicit no-HA profile in UI logic.
4. Add richer selection filters (album, favorites, people, date windows) on `/search/random`.
5. Add persistent cache strategy for last successful image and metadata.
