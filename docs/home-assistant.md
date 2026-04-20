---
title: Home Assistant Integration
description: Optionally integrate Espframe with Home Assistant for OTA updates and dashboard controls.
---

# Home Assistant Integration

Home Assistant is **not required** — the frame works standalone. If you run [Home Assistant](https://www.home-assistant.io/), you can add it as an ESPHome device for updates and dashboard control.

## Adding the Device

Espframe runs on [ESPHome](https://esphome.io/), so Home Assistant often discovers it automatically.

- **If discovered:** **Settings → Devices & Services** → **ESPHome: 1 device discovered** → **Configure** → **Submit**
- **If not:** **Add Integration** → **ESPHome** → enter the device IP (on screen or web UI) → **Submit**

## Exposed Entities

Under **Settings → Devices & Services → ESPHome** (device page):

| Entity | Type | Description |
|--------|------|-------------|
| **Photos: Source** | Select | All Photos, Favorites, Album, Person, Memories — see [Photo Sources](/photo-sources) |
| **Photos: Album IDs** | Text | Comma-separated Immich album UUIDs |
| **Photos: Album Labels** | Text | Optional friendly labels for saved album IDs |
| **Photos: Person IDs** | Text | Comma-separated Immich person UUIDs |
| **Photos: Person Labels** | Text | Optional friendly labels for saved person IDs |
| **Photos: Date Filter** | Switch | Turns photo date filtering on or off |
| **Photos: Date Filter Mode** | Select | Fixed Range or Relative Range |
| **Photos: Date From** | Text | Fixed range start date, in `YYYY-MM-DD` format |
| **Photos: Date To** | Text | Fixed range end date, in `YYYY-MM-DD` format |
| **Photos: Relative Amount** | Number | Rolling date range amount |
| **Photos: Relative Unit** | Select | Months or Years for the rolling date range |
| **Photos: Orientation** | Select | Any, Portrait Only, or Landscape Only |
| **Photos: Display Mode** | Select | Fill crops to cover the screen; Fit letterboxes without cropping |
| **Photos: Slideshow Interval** | Select | 30s–10min between photos |
| **Photos: Portrait Pairing** | Switch | Pair compatible portrait photos side-by-side |
| **Screen: Connection Timeout** | Select | 30s–30min before showing connection-failed screen |
| **Screen: Rotation** | Select | LVGL screen rotation: 0 or 180 degrees |
| **Screen: Backlight** | Light | On/off and brightness (0–100%) |
| **Firmware: Auto Update** | Switch | Install updates when available |
| **Firmware: Update Frequency** | Select | Hourly, Daily, Weekly, Monthly |
| **Firmware: Beta Channel** | Switch | Opt in to pre-release firmware checks |
| **Firmware: Check for Update** | Button | Manual check (stable + beta if enabled) |
| **Firmware: Version** | Text Sensor | Installed version |
| **Network: Online** | Binary Sensor | Connection status |
| **Network: WiFi Strength** | Sensor | Signal % |
| **Network: IP Address** | Text Sensor | Device IP |

## Automations

Use entities in automations, e.g.: turn **Screen: Backlight** on/off by presence; change **Photos: Slideshow Interval**, **Photos: Source**, or the date filter by time; notify when **Network: Online** goes unavailable; trigger **Firmware: Check for Update** from a script.
