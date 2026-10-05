---
title: EspFrame for Immich – ESP32 Digital Photo Frame
titleTemplate: :title
description: Build a standalone Immich digital photo frame on a Guition ESP32-P4 touchscreen with ESPHome. No hub, cloud, or extra software required.
---

# EspFrame for Immich

**EspFrame** is a standalone Immich digital photo frame for a supported Guition ESP32-P4 touchscreen. It turns an ESP32 photo frame into a private, self-hosted photo frame that displays your [Immich](https://immich.app/) library directly from your own server.

The firmware runs on ESP32-P4 hardware with [ESPHome](https://esphome.io/) and connects to Immich over HTTP or HTTPS. It does not need Home Assistant, a cloud account, or a separate bridge service.

Ready to get started? **[Install EspFrame](/install)**. Need a display first? [Choose a screen](/screens).

<img src="/espframe.png" alt="EspFrame displaying Immich photos on a Guition ESP32-P4 touchscreen" style="max-width: 100%; border-radius: 8px; margin: 1.5rem 0;" />

## What EspFrame Does

EspFrame firmware runs on the display itself with ESPHome and connects directly to your Immich server over HTTP or HTTPS to show photos from your own network. It supports all photos, favorites, albums, people, memories, and date-filtered selections. Home Assistant, a separate bridge app, and a cloud service are not required.

Use the frame's settings to adjust brightness and screen tone, schedule the display, pair portrait photos, and show a clock over the slideshow. The [photo sources guide](/photo-sources) explains the available sources and filters.

## What You Need

- A supported 10.1-inch Guition ESP32-P4 `JC8012P4A1` display.
- A working Immich server the frame can reach on your network or over HTTPS.
- An Immich API key; Read-only permissions are recommended.
- A USB-C data cable (not a charge-only cable) and a desktop computer running Chrome or Edge for browser installation with Web Serial.

See [supported screen revisions](/screens) and the full [installation requirements](/install#what-you-ll-need).

## Get Started

1. [Install EspFrame](/install) on the display.
2. Connect the frame to WiFi.
3. Enter your Immich server URL and [Immich API key](/api-key).
4. Choose the [photo sources](/photo-sources) for the slideshow.

For USB connection help, see [USB flashing](/usb-flashing). If setup does not work as expected, visit [troubleshooting](/troubleshooting).

## Privacy

EspFrame does not upload photos or send your library through a hosted service. The frame requests thumbnails and metadata from the Immich server URL you configure. If that server is only available on your local network, the frame stays local too.

## Features

- **Smart Photo Filters** — Combine albums, people, tags, favorites, ratings, dates, locations, exclusions, and orientation.
- **Display Tone Adjustment** — Adjust colour temperature so the panel looks right (e.g. warm the image if it’s too blue).
- **Night Tone** — Automatically adjust screen tone between sunset and sunrise.
- **Screen Scheduling** — Schedule when to turn off the display; set daytime and night-time brightness levels separately.
- **Portrait Pairing** — Automatically pairs portrait photos taken on the same day by default, with optional ±1-day or ±2-day matching for a side-by-side display that fills the screen edge-to-edge.
- **Clock Overlay** — Displays the current time over your photos when enabled in settings.
- **No Hub Required** — Connects directly to your Immich server over HTTP or HTTPS — no Home Assistant, cloud service, or extra software needed.

## Where to Buy

| Model | Panel | Stand |
|-------|-------|-------|
| Guition ESP32-P4 10.1-inch `JC8012P4A1` | [AliExpress](https://s.click.aliexpress.com/e/_c4LLo3rH) | [MakerWorld](https://makerworld.com/en/models/2490049-guition-p4-10inch-screen-stand#profileId-2736046) |

The [installer](/install) helps you choose the right firmware for your display.

## Support This Project

If you find this project useful, consider buying me a coffee to support ongoing development!

<a href="https://www.buymeacoffee.com/jtenniswood">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" height="60" style="border-radius:999px;" />
</a>