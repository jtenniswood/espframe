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

Use the table to choose **Hardware version** below, then click **Install**, select the display's serial port, and confirm. Keep it connected until installation finishes.

| What your display reports | Choose |
| --- | --- |
| ESP32-P4 chip revision **v3.x**, whatever the case marking | **V3** |
| Chip is not v3.x; rear-case number **2627 or lower** | **V1** |
| Chip is not v3.x; rear-case number **2628 or higher** | **V2** |

Unsure about the chip revision or case number? [Check your display](/screens/jc8012p4a1#identify-your-revision) before installing. If your matching option is missing, its browser firmware is not available yet.

**V3:** Initial USB installation and OTA updates have not yet been tested on physical V3 hardware.

<EspInstallButton />

The installer uses Web Serial; Safari and Firefox cannot flash the display. Having trouble? See [USB flashing help](/usb-flashing).

## 2. Connect to WiFi

Enter your WiFi name and password when prompted.

If no prompt appears, connect your phone or laptop to the frame's **espframe** WiFi hotspot and open `http://192.168.4.1`. This captive portal lets you enter your home WiFi details.

## 3. Connect to Immich

Open the **IP address shown on the display** in your browser. Enter **Immich Server URL** and **API Key**, then follow the setup wizard.

Use your server's IP address (for example, `http://192.168.1.30:2283`) or domain (`https://photos.example.com`). Choose your timezone and preferred clock format during setup.

Once photos appear, you can [choose albums and filters](/photo-sources) or [adjust the screen](/screen-settings).

<span id="recent-firmware-notes"></span>
Building with ESPHome? See [manual setup](/manual-setup).
