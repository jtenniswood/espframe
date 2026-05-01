# Espframe for Immich

A standalone digital photo frame that displays your [Immich](https://immich.app/) photo library on a Guition ESP32-P4 touchscreen — no additional services required.

<p align="center">
  <img src="docs/public/espframe.png" alt="Immich Frame displaying photos on a Guition ESP32-P4 touchscreen" width="700" />
</p>

Built with [ESPHome](https://esphome.io/) on the ESP32-P4, the frame connects directly to your Immich server over HTTP or HTTPS and streams photos to your display, completely in private over your local network.

## Features

- **Fully standalone** — runs on bare hardware, no hub or cloud service needed
- **Photo sources** — show all photos, favourites, specific albums, specific people, "on this day" memories, or date-filtered ranges
- **Display tone adjustment** — adjust colour temperature (e.g. warm the image if the panel looks too blue)
- **Night tone** — automatically adjust screen tone between sunset and sunrise
- **Screen scheduling** — schedule when to turn off the display; set daytime and night-time brightness separately
- **Smart portrait pairing** — detects portrait photos and pairs them side-by-side from the same day
- **Accent colour fill** — letterboxed areas are tinted with a colour sampled from the photo
- **Configuration options** — change Immich URL, API key, photo source, date filters, slideshow interval, and more via the built-in web UI

## Hardware

| Item | Link |
|------|------|
| 10" panel (JC8012P4A1) | [AliExpress](https://s.click.aliexpress.com/e/_c4LLo3rH) |
| 10" printable stand | [MakerWorld](https://makerworld.com/en/models/2490049-guition-p4-10inch-screen-stand#profileId-2736046) |

## Install

The easiest way to get started is with the **web installer** — flash the firmware directly from your browser with no toolchain or build step.

**[Open the Web Installer →](https://jtenniswood.github.io/espframe/install)**

You'll need a USB-C data cable and Chrome or Edge on desktop.

## Documentation

Getting started, configuration, and troubleshooting guides are available at:

**[jtenniswood.github.io/espframe](https://jtenniswood.github.io/espframe/)**

## Development

```bash
# Docs site (live reload)
npm ci
npm run docs:dev

# Compile firmware locally
docker run --rm -v "${PWD}:/config" ghcr.io/esphome/esphome:2026.4.0 compile /config/builds/guition-esp32-p4-jc8012p4a1.factory.yaml
```

### In-development firmware features

In-progress firmware experiences are built into normal firmware, but must stay off unless the hidden developer setting is enabled. Open the device web UI with `?developer=1`, for example `http://<device-ip>/?developer=1`, then use the **Developer** panel to turn them on for that device.

Firmware code should check `id(developer_features_enabled).state` before running anything experimental. The switch defaults off and persists only when deliberately enabled.

## Support This Project

If you find this project useful, consider buying me a coffee to support ongoing development!

<a href="https://www.buymeacoffee.com/jtenniswood">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" height="60" style="border-radius:999px;" />
</a>
