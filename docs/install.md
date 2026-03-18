---
title: Install
description: Flash Espframe for Immich firmware to your ESP32-P4 directly from the browser using Web Serial.
---

# Install

Flash Espframe to a Guition ESP32-P4 10" display from your browser — no desktop toolchain or ESPHome required.

## What You'll Need

- **Guition ESP32-P4 10" display** (JC8012P4A1), **USB-C cable** (data-capable), **Immich server** on your network ([immich.app](https://immich.app/)), and an [**Immich API key**](./api-key)

**Buy:** [Panel (AliExpress)](https://s.click.aliexpress.com/e/_c4LLo3rH) · [Stand (MakerWorld)](https://makerworld.com/en/models/2490049-guition-p4-10inch-screen-stand#profileId-2736046)

## Web Installer

Connect the display via USB-C, then click the button below.

<EspInstallButton />

::: info Browser
Requires **Chrome** or **Edge** (desktop) with [WebSerial](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API). Safari and Firefox not supported.
:::

## Steps

1. **Connect** — Plug in with USB-C; allow drivers if prompted.
2. **Flash** — Click **Install Espframe for Immich**, choose the device’s serial port, confirm. Takes a few minutes.
3. **WiFi** — Enter network name and password when prompted. If no prompt appears, the device creates hotspot **immich-frame-10inch**; connect from phone/laptop for captive-portal setup.
4. **Immich** — Open the device IP in a browser (shown on screen), enter **Immich server URL** and **API key**. See [API Key](/api-key) for permissions. Photos start loading. Next: [Photo Sources](/photo-sources) to choose what to display.