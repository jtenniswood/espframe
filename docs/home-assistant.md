---
title: Home Assistant Integration
description: Optionally integrate Espframe for Immich with Home Assistant for OTA updates and dashboard controls.
---

# Home Assistant

Home Assistant is **not required** to use Espframe for Immich — the device works fully standalone with its built-in web UI. However, if you already run [Home Assistant](https://www.home-assistant.io/), you can add the frame as an ESPHome device to manage updates and change settings from your dashboard instead.

## Adding the Device

Because Espframe for Immich runs on [ESPHome](https://esphome.io/), Home Assistant will usually discover it automatically.

### 1. Check for auto-discovery

Open **Settings → Devices & Services** in Home Assistant. If the device has been discovered, you'll see a notification:

> **ESPHome: 1 device discovered**

Click **Configure**, then **Submit** to add the device. No API key or manual setup is needed.

### 2. If the device is not discovered

If the device doesn't appear automatically:

1. Go to **Settings → Devices & Services → Add Integration**
2. Search for **ESPHome**
3. Enter the device's IP address (shown on the frame's screen or in the web UI) and click **Submit**

## Exposed Entities

Once added, the frame exposes the following entities in Home Assistant. You can view and control them from the device page under **Settings → Devices & Services → ESPHome**. Settings that are only in the built-in web UI (e.g. clock, schedule, connection URL) are not exposed as entities.

### Photos

| Entity | Type | Description |
|---|---|---|
| **Photos: Source** | Select | Which photos to display: All Photos, Favorites, Album, Person, or Memories — see [Photo Sources](/photo-sources) for setup |
| **Photos: Slideshow Interval** | Select | Time between photos: 10 seconds, 15 seconds, 20 seconds, 30 seconds, 45 seconds, 1–10 minutes |

### Lights

| Entity | Type | Description |
|---|---|---|
| **Screen: Backlight** | Light | Screen on/off and brightness (0–100%). Use in automations to control the display. |

### Firmware

| Entity | Type | Description |
|---|---|---|
| **Firmware: Auto Update** | Switch | Automatically install firmware updates when available |
| **Firmware: Update Frequency** | Select | How often to check: `Hourly`, `Daily`, or `Weekly` |
| **Firmware: Check for Update** | Button | Manually trigger a check for updates (stable and beta) |
| **Firmware: Version** | Text Sensor | Currently installed firmware version |

### Network (diagnostics)

| Entity | Type | Description |
|---|---|---|
| **Network: Online** | Binary Sensor | Whether the device is connected (diagnostic) |
| **Network: WiFi Strength** | Sensor | WiFi signal strength as a percentage (0–100%) |
| **Network: WiFi Signal dB** | Sensor | Raw WiFi signal in dBm (diagnostic, disabled by default — enable in entity settings if needed) |
| **Network: IP Address** | Text Sensor | Device IP address (diagnostic) |

## Automations

Because the frame is a native ESPHome device, you can use its entities in Home Assistant automations. Some ideas:

- **Control screen from presence** — turn `Screen: Backlight` on when someone arrives and off when everyone leaves
- **Adjust slideshow speed** — change `Photos: Slideshow Interval` in the evening for a slower pace
- **Switch photo source** — set `Photos: Source` to `Memories` on weekends (see [Photo Sources](/photo-sources))
- **Notify on disconnect** — trigger when the frame or `Network: Online` goes unavailable
- **Manual update check** — call the `Firmware: Check for Update` button from a script or automation