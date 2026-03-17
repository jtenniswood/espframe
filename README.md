# Espframe for Immich

A standalone digital photo frame that displays your [Immich](https://immich.app/) photo library on a 10" touchscreen — no additional services required.

<p align="center">
  <img src="docs/public/immich-frame.png" alt="Immich Frame displaying photos on a Guition ESP32-P4 10-inch screen" width="700" />
</p>

Built with [ESPHome](https://esphome.io/) on the ESP32-P4, the frame connects directly to your Immich server over HTTP and streams photos to your display, completely in private over your local network.

## Features

- **Fully standalone** — runs on bare hardware, no hub or cloud service needed
- **Photo filtering** — show all photos, favourites, specific albums, specific people, or "on this day" memories
- **Smart portrait pairing** — detects portrait photos and pairs them side-by-side from the same day
- **Accent colour fill** — letterboxed areas are tinted with a colour sampled from the photo
- **Configuration options** — change Immich URL, API key, slideshow interval, and more via the built-in web UI

## Hardware

| Item | Link |
|------|------|
| Panel | [AliExpress](https://s.click.aliexpress.com/e/_c4LLo3rH) |
| Printable stand | [MakerWorld](https://makerworld.com/en/models/2490049-guition-p4-10inch-screen-stand#profileId-2736046) |

## Install

The easiest way to get started is with the **web installer** — flash the firmware directly from your browser with no toolchain or build step.

**[Open the Web Installer →](https://jtenniswood.github.io/espframe/install)**

You'll need a USB-C data cable and Chrome or Edge on desktop.

## Documentation

Getting started, configuration, and troubleshooting guides are available at:

**[jtenniswood.github.io/espframe](https://jtenniswood.github.io/espframe/)**

## Support This Project

If you find this project useful, consider buying me a coffee to support ongoing development!

<a href="https://www.buymeacoffee.com/jtenniswood">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" height="50" />
</a>
