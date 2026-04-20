---
title: Install
description: Flash Espframe for Immich firmware to a supported ESP32-P4 display directly from the browser using Web Serial.
---

# Install

Flash Espframe to a supported Guition ESP32-P4 display from your browser — no desktop toolchain or ESPHome required.

## What You'll Need

- **Supported Guition ESP32-P4 display**, **USB-C cable** (data-capable), **Immich server** on your network ([immich.app](https://immich.app/)), and an [**Immich API key**](./api-key)

| Model | Panel | Stand |
|-------|-------|-------|
| Guition ESP32-P4 10" `JC8012P4A1` | [AliExpress](https://s.click.aliexpress.com/e/_c4LLo3rH) | [MakerWorld](https://makerworld.com/en/models/2490049-guition-p4-10inch-screen-stand#profileId-2736046) |

## Connect the Display

The device has two USB-C ports. Plug the cable into the **bottom port** (labeled **USB** on the PCB) — the one closest to the edge, next to the USB-A connector. The upper port is for the screen ribbon cable only.

<img src="/usb-plug.png" alt="USB-C cable plugged into the bottom USB-C port of the Guition ESP32-P4" style="max-width: 100%; border-radius: 8px; margin: 1rem 0;" />

::: tip Wrong port?
If flashing fails, make sure you're using the **bottom** USB-C port as shown above. The upper port will not work for flashing.
:::

## Web Installer

Connect the display via USB-C, then click the install button below. Check the model printed in the listing or on the PCB before continuing.

<EspInstallButton />

::: info Browser
Requires **Chrome** or **Edge** (desktop) with [WebSerial](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API). Safari and Firefox not supported.
:::

## Steps

1. **Connect** — Plug in with USB-C; allow drivers if prompted.
2. **Flash** — Click **Install Espframe for Immich**, choose the device’s serial port, confirm. Takes a few minutes.
3. **WiFi** — Enter network name and password when prompted. If no prompt appears, the device creates a hotspot named **immich-frame-10inch**; connect from phone/laptop for captive-portal setup.
4. **Immich** — Open the device IP in a browser (shown on screen), enter **Immich server URL** and **API key**. The URL can be an IP address such as `http://192.168.1.30:2283` or a domain such as `https://photos.example.com`. See [API Key](/api-key) for permissions. Photos start loading. Next: [Photo Sources](/photo-sources) to choose what to display.

## Recent firmware notes

- **Multiple Person or Album IDs:** Saving comma-separated UUID lists uses a POST body so long lists no longer hit **414 URI Too Long**. Album IDs and Person IDs are still limited to **255 characters** each; see [Photo Sources](/photo-sources#album-and-person-id-limits).
- **Photo date filters:** The web UI now supports fixed date ranges and rolling ranges such as the last 6 months or last 2 years. See [Photo Sources](/photo-sources#date-filtering).
- **ESPHome 2026.4:** Current local builds use ESPHome `2026.4.0`; manual builds also include compatibility fixes for ESPHome 2026.3 and 2026.4 LVGL changes.
