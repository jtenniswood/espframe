---
title: Espframe Home Assistant Integration
description: Add Espframe to Home Assistant as an ESPHome device for optional OTA updates, controls, and dashboard visibility.
---

# Espframe Home Assistant Integration

Home Assistant is **not required** — the frame works standalone. If you run [Home Assistant](https://www.home-assistant.io/), you can add it as an ESPHome device for updates and dashboard control.

## Adding the Device

Espframe runs on [ESPHome](https://esphome.io/), so Home Assistant often discovers it automatically.

- **If discovered:** **Settings → Devices & Services** → **ESPHome: 1 device discovered** → **Configure** → **Submit**
- **If not:** **Add Integration** → **ESPHome** → enter the device IP (on screen or web UI) → **Submit**

## Exposed Entities

Under **Settings → Devices & Services → ESPHome** (device page):

| Entity | Type | Description |
|--------|------|-------------|
| **Photos: Source** | Select | All Photos, Favorites, Album, Person, Tag, Memories — see [Photo Sources](/photo-sources) |
| **Photos: Album IDs** | Text | Comma-separated Immich album UUIDs |
| **Photos: Album Labels** | Text | Optional friendly labels for saved album IDs |
| **Photos: Person IDs** | Text | Comma-separated Immich person UUIDs |
| **Photos: Person Labels** | Text | Optional friendly labels for saved person IDs |
| **Photos: Tag IDs** | Text | Comma-separated Immich tag UUIDs |
| **Photos: Tag Labels** | Text | Optional friendly labels for saved tag IDs |
| **Photos: Date Filter** | Switch | Turns photo date filtering on or off |
| **Photos: Date Filter Mode** | Select | Fixed Range or Relative Range |
| **Photos: Date From** | Text | Fixed range start date, in `YYYY-MM-DD` format |
| **Photos: Date To** | Text | Fixed range end date, in `YYYY-MM-DD` format |
| **Photos: Relative Amount** | Number | Rolling date range amount |
| **Photos: Relative Unit** | Select | Months or Years for the rolling date range |
| **Photos: Orientation** | Select | Any, Portrait Only, or Landscape Only |
| **Photos: Display Mode** | Select | Fill crops to cover the screen; Fit letterboxes without cropping |
| **Photos: Slideshow Interval** | Select | 10s–10min between photos |
| **Photos: Portrait Pairing** | Switch | Pair compatible portrait photos side-by-side |
| **Screen: Connection Timeout** | Select | 30s–30min before showing connection-failed screen |
| **Screen: Rotation** | Select | LVGL screen rotation: 0 or 180 degrees |
| **Screen: Backlight** | Light | On/off and brightness (0–100%). Turning it off puts the frame to sleep; turning it on wakes manual sleep unless scheduled off-hours are active. |
| **Screen: Sleep** | Button | Puts the display into the same sleep state as the touchscreen hold gesture, pausing slideshow fetches. |
| **Screen: Wake** | Button | Wakes the display and resumes the slideshow. During scheduled off-hours, this is a temporary wake using the configured wake timeout. |
| **Firmware: Auto Update** | Switch | Install updates when available |
| **Firmware: Update Frequency** | Select | Hourly, Daily, Weekly, Monthly |
| **Firmware: Check for Update** | Button | Manual stable update check |
| **Firmware: Version** | Text Sensor | Installed version |
| **Network: Online** | Binary Sensor | Connection status |
| **Network: WiFi Strength** | Sensor | Signal % |
| **Network: IP Address** | Text Sensor | Device IP |
| **Reset Reason** | Text Sensor | Last reboot reason |

## Automations

Use entities in automations, e.g.: press **Screen: Sleep** when a room becomes empty and **Screen: Wake** when it becomes occupied; change **Photos: Slideshow Interval**, **Photos: Source**, or the date filter by time; notify when **Network: Online** goes unavailable; trigger **Firmware: Check for Update** from a script.
