---
title: Home Assistant Integration
description: Optionally integrate Espframe for Immich with Home Assistant for OTA updates and dashboard controls.
---

# Home Assistant

Home Assistant is **not required** to use Espframe for Immich — the device works fully standalone with its built-in [web UI](/configuration). However, if you already run [Home Assistant](https://www.home-assistant.io/), you can add the frame as an ESPHome device to manage updates and change settings from your dashboard instead.

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
3. Enter the device's IP address (shown on the frame's screen) and click **Submit**

## What You Get

Once added, the frame exposes its settings as Home Assistant entities. You can view and control them from the device page under **Settings → Devices & Services → ESPHome**.

### Photos

| Entity | Type | Description |
|---|---|---|
| **Photos: Slideshow Interval** | Select | Time between photos (10 seconds – 10 minutes) |
| **Photos: Source** | Select | Which photos to display — see [Photo Sources](/photo-sources) for options and setup |

### Lights

| Entity | Type | Description |
|---|---|---|
| **Screen: Backlight** | Light | Screen on/off and brightness (0–100%). Use in automations to control the display. |

### Firmware

| Entity | Type | Description |
|---|---|---|
| **Firmware: Auto Update** | Switch | Automatically install firmware updates |
| **Firmware: Update Frequency** | Select | How often to check: `Hourly`, `Daily`, or `Weekly` |

### Sensors

| Entity | Type | Description |
|---|---|---|
| **WiFi Signal** | Sensor | Current signal strength (dBm) |
| **Firmware: Version** | Text Sensor | Currently installed firmware version |

## Automations

Because the frame is a native ESPHome device, you can use it in Home Assistant automations like any other entity. A few ideas:

- **Turn off the clock at night** — toggle `Clock: Show` during sleeping hours
- **Control screen brightness** — use the `Screen: Backlight` light to turn the display on/off or set brightness in automations
- **Adjust slideshow speed** — slow the `Photos: Slideshow Interval` in the evening for a more relaxed pace
- **Switch to memories on weekends** — set `Photos: Source` to `Memories` (see [Photo Sources](/photo-sources))
- **Notify on disconnect** — send an alert if the frame goes offline

## What's next

For more planned Home Assistant features, check the [Roadmap](/roadmap). If you prefer to manage settings without Home Assistant, see the [Configuration](/configuration) guide for the built-in web UI.
