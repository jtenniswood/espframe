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
| **Photos: Slideshow Interval** | Select | 30s–10min between photos |
| **Screen: Connection Timeout** | Select | 30s–30min before showing connection-failed screen |
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

Use entities in automations, e.g.: turn **Screen: Backlight** on/off by presence; change **Photos: Slideshow Interval** or **Photos: Source** by time; notify when **Network: Online** goes unavailable; trigger **Firmware: Check for Update** from a script.
