---
title: Immich Digital Photo Frame with Espframe
description: Use Espframe to build a private Immich digital photo frame on a Guition ESP32-P4 touchscreen without a cloud service or extra hub.
---

# Immich Digital Photo Frame with Espframe

Espframe turns a supported Guition ESP32-P4 touchscreen into a standalone digital photo frame for [Immich](https://immich.app/). It is designed for self-hosted photo libraries, so the frame connects directly to your Immich server and displays photos from your own network.

Unlike a tablet dashboard or cloud photo frame, Espframe runs firmware built with [ESPHome](https://esphome.io/) on the display hardware itself. You do not need Home Assistant, a separate bridge app, or a subscription service.

<img src="/espframe.png" alt="Espframe Immich digital photo frame on a Guition ESP32-P4 touchscreen" style="max-width: 100%; border-radius: 8px; margin: 1rem 0;" />

## What Espframe Does

- Shows photos from your Immich library on an ESP32-P4 touchscreen.
- Connects directly to Immich over HTTP or HTTPS.
- Supports all photos, favorites, albums, people, memories, and date-filtered ranges.
- Includes display controls for brightness, schedules, screen tone, and touch gestures.
- Uses a browser-based installer for the supported Guition ESP32-P4 10-inch display.

## What You Need

| Item | Notes |
|------|-------|
| Immich server | A working self-hosted Immich instance on your network or reachable by HTTPS |
| Supported display | Guition ESP32-P4 10-inch `JC8012P4A1` |
| USB-C data cable | Used for first-time flashing |
| Chrome or Edge | Required for browser flashing with Web Serial |
| Immich API key | Read-only permissions are recommended |

## How Setup Works

1. Flash the firmware from the [Install Espframe on a Guition ESP32-P4 Display](/install) page.
2. Connect the frame to WiFi.
3. Enter your Immich server URL and [Immich API key permissions](/api-key).
4. Choose which [Espframe photo sources for Immich](/photo-sources) should appear in the slideshow.

## Privacy Model

Espframe does not upload photos or send your library through a hosted service. The frame requests thumbnails and metadata from the Immich server URL you configure. If your Immich instance is only available on your local network, the frame stays local too.

## Related Guides

- [Install Espframe on a Guition ESP32-P4 Display](/install)
- [USB Flashing Help for Guition ESP32-P4](/usb-flashing)
- [Immich API Key Permissions for Espframe](/api-key)
- [Troubleshooting Espframe](/troubleshooting)
