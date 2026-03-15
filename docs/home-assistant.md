---
title: Home Assistant Integration
description: Optionally integrate Immich Frame with Home Assistant for OTA updates and dashboard controls.
---

# Home Assistant

Home Assistant is **not required** to use Immich Frame — the device works fully standalone with its built-in [web UI](/configuration). However, if you already run [Home Assistant](https://www.home-assistant.io/), you can add the frame as an ESPHome device to manage updates and change settings from your dashboard instead.

## Adding the Device

Because Immich Frame runs on [ESPHome](https://esphome.io/), Home Assistant will usually discover it automatically.

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

### Controls

| Entity | Type | Description |
|---|---|---|
| **Slideshow Interval** | Number | Time between photos (5–300 seconds) |
| **Show Clock** | Switch | Toggle the clock overlay |
| **Clock Format** | Select | `24 Hour` or `12 Hour` |
| **Timezone** | Select | Device timezone |

### Sensors

| Entity | Type | Description |
|---|---|---|
| **WiFi Signal** | Sensor | Current signal strength (dBm) |
| **Uptime** | Sensor | Time since last reboot |

## OTA Updates

When a new firmware version is available, Home Assistant will show an update notification on the device page. Click **Update** to flash the new firmware over-the-air — no USB cable required.

You can also trigger OTA updates from **Settings → Devices & Services → ESPHome**, selecting the device, and clicking **Update** in the firmware section.

## Automations

Because the frame is a native ESPHome device, you can use it in Home Assistant automations like any other entity. A few ideas:

- **Turn off the clock at night** — disable the clock overlay during sleeping hours
- **Adjust slideshow speed** — slow the interval in the evening for a more relaxed pace
- **Notify on disconnect** — send an alert if the frame goes offline
