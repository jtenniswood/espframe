---
title: Install Espframe on a Guition ESP32-P4 Display
description: Flash Espframe for Immich firmware to a supported Guition ESP32-P4 touchscreen directly from Chrome or Edge using Web Serial.
---

# Install Espframe on a Guition ESP32-P4 Display

Flash Espframe to a supported Guition ESP32-P4 display from your browser — no desktop toolchain or ESPHome required.

## Before You Start {#what-you-ll-need}

- Identify your exact [supported display and hardware revision](/screens).
- Use Chrome or Edge on a desktop computer and a USB-C data cable, not a charge-only cable.
- Have your Immich server URL and [API key](/api-key) ready.

Browser installation does not require ESPHome or a local firmware build. You will flash the matching firmware, connect the frame to WiFi, then enter the Immich server details.

## Supported Display

Espframe supports the Guition ESP32-P4 10.1-inch `JC8012P4A1` in V1, V2, and V3 revisions. See [Choose a Screen](/screens) to check support, distinguish the revisions, and find purchase and stand information.

Panel: [AliExpress](https://s.click.aliexpress.com/e/_c4LLo3rH). Stand: [MakerWorld](https://makerworld.com/en/models/2490049-guition-p4-10inch-screen-stand#profileId-2736046).

## Choose the Correct Panel Firmware

The 10-inch `JC8012P4A1` has three firmware profiles. Check the ESP32-P4 chip revision first; ESPHome logs or `esptool` chip information may identify it:

- **ESP32-P4 v3.x:** choose **V3**, regardless of the rear-case marking.
- If the chip is not V3, rear-case marking **`2627` or lower:** choose **V1**.
- If the chip is not V3, rear-case marking **`2628` or higher:** choose **V2**.

The case marking is a fallback for distinguishing V1 and V2 when chip information does not identify V3. See the [10-inch model guide](/screens/jc8012p4a1) and the [V1](/screens/jc8012p4a1-v1), [V2](/screens/jc8012p4a1-v2), and [V3](/screens/jc8012p4a1-v3) setup pages before choosing.

## Connect the Display

The device has two USB-C ports. Plug the cable into the **bottom port** (labeled **USB** on the PCB) — the one closest to the edge, next to the USB-A connector. The upper port is for the screen ribbon cable only.

<img src="/usb-plug.png" alt="USB-C cable plugged into the bottom USB-C flashing port on the Guition ESP32-P4 display" style="max-width: 100%; border-radius: 8px; margin: 1rem 0;" />

::: tip Wrong port?
If flashing fails, make sure you're using the **bottom** USB-C port as shown above. The upper port will not work for flashing.
:::

## Flash the Firmware

Connect the display with USB-C, choose the matching hardware version in the installer, then click install. Nothing is selected by default, so the installer stays unavailable until you choose a version. V3 is identified from chip information and takes precedence over the case marking.

<EspInstallButton />

::: info Browser
Requires **Chrome** or **Edge** on a desktop computer with [Web Serial](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API). Safari and Firefox not supported.
:::

## Steps

1. **Connect** — Plug in with USB-C; allow drivers if prompted.
2. **Flash** — Select **V1**, **V2**, or **V3**, then click **Install**. Choose the device’s serial port and confirm. Takes a few minutes.
3. **WiFi** — Enter network name and password when prompted. If no prompt appears, open the WiFi settings on your phone or laptop and look for the frame’s WiFi hotspot: a network starting with **ESP_**, such as **ESP_7A1EED**. The letters and numbers after **ESP_** come from the frame’s MAC address (its network identifier), so your frame’s name will be different. Connect to that network, then follow the setup page (captive portal) to enter your home WiFi details. If the page does not open automatically, visit `http://192.168.4.1`.
4. **Immich** — Open the device IP in a browser (shown on screen), enter **Immich Server URL** and **API Key**. The URL can be an IP address such as `http://192.168.1.30:2283` or a domain such as `https://photos.example.com`. See [API Key](/api-key) for permissions. Photos start loading. Next: [Smart Photo Filters](/photo-sources) to choose what to display.

The setup wizard defaults to **24 Hour** clock format, **Europe/London (GMT+0)** timezone, shows the clock by default, and uses **0.pool.ntp.org**, **1.pool.ntp.org**, and **2.pool.ntp.org** for time sync. Pick your timezone during setup so the clock and sunrise/sunset based brightness and night tone are calculated for your location. The on-screen clock refreshes every **60 seconds**.

Choose whether the on-screen clock uses a 24-hour or 12-hour format.

## Recent firmware notes

- **Multiple Album, Person, or Tag IDs:** Saving comma-separated UUID lists uses a POST body so long lists no longer hit **414 URI Too Long**. Album IDs, Person IDs, and Tag IDs are still limited to **255 characters** each; see [Photo Sources](/photo-sources#album-person-and-tag-id-limits).
- **Photo date filters:** The web UI now supports fixed date ranges and rolling ranges such as the last 6 months or last 2 years. See [Photo Sources](/photo-sources#date-filtering).
- **ESPHome 2026.9:** Current local builds use ESPHome `2026.9.1`; manual builds also include compatibility fixes for ESPHome 2026.3, 2026.4, and 2026.7 LVGL changes.
