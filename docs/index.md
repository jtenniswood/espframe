---
title: Espframe for Immich – ESP32 Digital Photo Frame
titleTemplate: :title
description: Standalone Immich-powered digital photo frame on ESP32-P4. No hub, cloud, or extra software required.
---

# Espframe for Immich

**Espframe** is a standalone digital photo frame that displays your [Immich](https://immich.app/) photo library on a supported Guition ESP32-P4 touchscreen. It runs on ESP32-P4 hardware with [ESPHome](https://esphome.io/) and connects directly to your self-hosted Immich server over HTTP or HTTPS — no hub, cloud, or extra software required.

<img src="/espframe.png" alt="Espframe displaying photos on a Guition ESP32-P4 touchscreen" style="max-width: 100%; border-radius: 8px; margin: 1.5rem 0;" />

## Features

- **Photo Sources** — Show all photos, favorites only, specific albums, specific people, "on this day" memories, or photos within a date range.
- **Display Tone Adjustment** — Adjust colour temperature so the panel looks right (e.g. warm the image if it’s too blue).
- **Night Tone** — Automatically adjust screen tone between sunset and sunrise.
- **Screen Scheduling** — Schedule when to turn off the display; set daytime and night-time brightness levels separately.
- **Portrait Pairing** — Automatically pairs portrait photos taken on the same day for a side-by-side display that fills the screen edge-to-edge.
- **Accent Color Fill** — Replaces black letterbox bars with a muted color sampled from the photo itself.
- **Clock Overlay** — Displays the current time over your photos when enabled in settings.
- **No Hub Required** — Connects directly to your Immich server over HTTP or HTTPS — no Home Assistant, cloud service, or extra software needed.

## Where to Buy

| Model | Panel | Stand |
|-------|-------|-------|
| Guition ESP32-P4 10" `JC8012P4A1` | [AliExpress](https://s.click.aliexpress.com/e/_c4LLo3rH) | [MakerWorld](https://makerworld.com/en/models/2490049-guition-p4-10inch-screen-stand#profileId-2736046) |

## Support This Project

If you find this project useful, consider buying me a coffee to support ongoing development!

<a href="https://www.buymeacoffee.com/jtenniswood">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" height="60" style="border-radius:999px;" />
</a>
