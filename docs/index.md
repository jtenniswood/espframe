---
title: Espframe for Immich – ESP32 Digital Photo Frame
titleTemplate: :title
description: Build a standalone Immich digital photo frame on a Guition ESP32-P4 touchscreen with ESPHome. No hub, cloud, or extra software required.
---

# Espframe for Immich

**Espframe** is a standalone Immich digital photo frame for a supported Guition ESP32-P4 touchscreen. It turns an ESP32 photo frame into a private, self-hosted photo frame that displays your [Immich](https://immich.app/) library directly from your own server.

The firmware runs on ESP32-P4 hardware with [ESPHome](https://esphome.io/) and connects to Immich over HTTP or HTTPS. It does not need Home Assistant, a cloud account, or a separate bridge service.

<img src="/espframe.png" alt="Espframe displaying Immich photos on a Guition ESP32-P4 touchscreen" style="max-width: 100%; border-radius: 8px; margin: 1.5rem 0;" />

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
| Guition ESP32-P4 10" `JC8012P4A1`, original (`2627` or lower) and new (`2628` or higher) panels | [AliExpress](https://s.click.aliexpress.com/e/_c4LLo3rH) | [MakerWorld](https://makerworld.com/en/models/2490049-guition-p4-10inch-screen-stand#profileId-2736046) |

## Support This Project

If you find this project useful, consider buying me a coffee to support ongoing development!

<a href="https://www.buymeacoffee.com/jtenniswood">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" height="60" style="border-radius:999px;" />
</a>
