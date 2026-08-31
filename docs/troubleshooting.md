---
title: Troubleshooting Espframe Install and Immich Setup
description: Fix common Espframe setup problems, including web installer failures, WiFi setup, Immich connection errors, API key permissions, and missing photos.
---

# Troubleshooting Espframe Install and Immich Setup

Use this page when Espframe does not flash, connect to WiFi, reach Immich, or show photos from your library.

## Web Installer Problems

If the browser installer cannot find the display, first check the basics:

- Use Chrome or Edge on a desktop computer.
- Use the bottom USB-C port on the Guition ESP32-P4 display.
- Use a USB-C data cable, not a charge-only cable.
- Close any other serial monitor, ESPHome dashboard, or flashing tool.

See [USB Flashing Help for Guition ESP32-P4](/usb-flashing) for more detail.

## WiFi Setup Problems

After flashing, Espframe should either ask for WiFi details or create **immich-frame-10inch** (original panel) or **immich-frame-10inch-v2** (new panel).

If you do not see the frame on your network:

- Check that the WiFi name and password were entered correctly.
- Make sure the frame is close enough to the access point.
- Look again for the **immich-frame-10inch** or **immich-frame-10inch-v2** hotspot from a phone or laptop.
- Reboot the frame and wait for the setup screen to appear.

## Immich Connection Problems

The Immich server URL must include `http://` or `https://`. Local IP addresses and domain names both work, for example:

```text
http://192.168.1.30:2283
https://photos.example.com
```

If the frame cannot connect:

- Open the same URL from a browser on the same network.
- Confirm the Immich server is running.
- Check that the frame and Immich server can reach each other across your network.
- If using HTTPS, confirm the address works cleanly from another device.

## API Key Problems

Espframe needs a read-only Immich API key. If the key is missing permissions, photos or metadata may fail to load.

Create a fresh key using the recommended [Immich API key permissions for Espframe](/api-key), then paste it into the frame web UI.

## Home Assistant Reports “Connection Requires Encryption”

This message concerns the ESPHome API encryption key shared by Home Assistant and the frame, not the Immich API key used to load photos.

1. Update Home Assistant to **2026.8.1** or newer so dynamically provisioned keys are synchronized with ESPHome Device Builder.
2. In **Settings → Devices & Services → ESPHome**, open the Espframe integration and choose **Reconfigure**. Enter the ESPHome encryption key if Home Assistant requests it.
3. If the frame was adopted in ESPHome Device Builder, confirm its `api → encryption → key` matches the key Home Assistant is using before installing another Device Builder build.
4. If the key was lost during a full erase or factory reinstall, remove and add the ESPHome integration again so Home Assistant can provision a new per-device key.

Normal Espframe OTA updates preserve the stored key. Do not paste the Immich API key into Home Assistant's encryption-key prompt.

## Photos Do Not Appear

If the frame connects but does not show the photos you expect:

- Start with **All Photos** as the source to confirm the basic connection works.
- Check that favorites, albums, people, or memories exist in Immich before selecting those sources.
- Confirm album and person UUIDs were copied from the Immich URL correctly.
- Review [Espframe Smart Photo Filters for Immich](/photo-sources) for filter rules and version compatibility.
- Disable date filtering temporarily if the selected range may exclude all photos.

## Screen or Display Issues

If the image is distorted immediately after boot, the wrong panel firmware is probably installed. Check the four-digit rear-case marking: `2627` or lower uses the original-panel firmware; `2628` or higher uses the new-panel firmware. Reinstall the matching profile from the [web installer](/install).

Display behavior is configured from the frame web UI:

- Use [screen brightness and display settings](/screen-settings) for brightness, schedules, and rotation.
- Use [screen tone and night warmth](/screen-tone) if the panel looks too blue or too warm.
- Use [touch controls](/touch-controls) if you need wake, sleep, or next-photo gestures, including left/right image-set navigation.

## Manual ESPHome Builds

If you are building locally instead of using the web installer, start with [ESPHome Manual Setup for Espframe](/manual-setup). Manual setup is useful when you want direct control over YAML substitutions, secrets, and local build behavior.
