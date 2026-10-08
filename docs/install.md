---
title: Install EspFrame
description: Choose the right firmware for your Guition display, install it from your browser, and connect EspFrame to WiFi and Immich.
---

<span id="install-espframe-on-a-guition-esp32-p4-display"></span>

# Install EspFrame

<span id="supported-display"></span>

## Before You Start {#what-you-ll-need}

You need a **Guition JC8012P4A1** display, a **USB-C data cable** (not a charge-only cable), and **Chrome or Edge on a desktop computer**. Have your Immich server URL and [API key](/api-key) ready.

Need a display? [Buy the panel](https://s.click.aliexpress.com/e/_c4LLo3rH) or [print a stand](https://makerworld.com/en/models/2490049-guition-p4-10inch-screen-stand#profileId-2736046).

<span id="connect-the-display"></span>
<span id="flash-the-firmware"></span>
<span id="web-installer"></span>

## 1. Install the Firmware {#steps}

Plug the cable into the **bottom USB-C port**, next to the USB-A connector. The upper port is for the screen ribbon cable.

<img src="/usb-plug.png" alt="USB-C cable plugged into the bottom USB-C flashing port on the Guition ESP32-P4 display" style="max-width: 100%; border-radius: 8px; margin: 1rem 0;" />

### Choose the Correct Panel Firmware

<EspInstallButton />

::: info Browser and USB help
The installer uses Web Serial; Safari and Firefox cannot flash the display. Having trouble? See [USB flashing help](/usb-flashing).
:::

## 2. Connect to WiFi

Keep the display connected to USB after flashing. The installer should offer a WiFi setup form in the same browser tab; enter your WiFi name and password there.

If the WiFi form does not appear, or you are setting up the frame without USB, connect your phone or laptop to the frame's **espframe** WiFi hotspot. If the captive portal does not open automatically, visit `http://192.168.4.1` and enter your home WiFi details.

## 3. Connect to Immich

Open the **IP address shown on the display** in your browser. Enter **Immich Server URL** and **API Key**, then follow the setup wizard.

Use your server's IP address (for example, `http://192.168.1.30:2283`) or domain (`https://photos.example.com`). Choose your timezone and preferred clock format during setup.
