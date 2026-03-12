# Espframe for Immich

<img src="/immich-frame.png" alt="Immich Frame displaying photos on a 10-inch touchscreen" style="border-radius: 12px; margin: 1em 0;" />

Espframe is a standalone digital photo frame powered by [ESPHome](https://esphome.io/) on the ESP32-P4. It connects directly to your [Immich](https://immich.app/) server over HTTP and streams your photo library to a 10" touchscreen — no hub, cloud service, or extra software required. Photos advance automatically with smooth transitions, and touch gestures let you swipe through your library at any time.

<a href="./install" class="VPButton medium brand" style="display: inline-block; margin-top: 1em;">Get Started →</a>

## Clock Overlay

When **Show Clock** is enabled in [settings](./configuration#clock), the current time is displayed in the bottom-left corner of the screen.

## Portrait Pairing

When a portrait (vertical) photo comes up, the frame automatically searches your Immich library for another portrait taken on the same day. If one is found, both portraits are displayed **side by side**, filling the widescreen display much better than a single letterboxed portrait. If no companion portrait is found for that day, the photo is shown on its own.

## Accent Color Fill

When a photo doesn't fill the entire screen (letterboxing), the black bars are replaced with a muted colour sampled from the photo itself. This makes transitions feel smoother and the display more cohesive.

## Slideshow

Photos are fetched randomly from your Immich library and advance automatically based on the **Slideshow Interval** set in [configuration](./configuration#slideshow). The frame prefetches upcoming images in the background so transitions are instant.
