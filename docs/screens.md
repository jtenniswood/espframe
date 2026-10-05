---
title: Choose a Screen for Espframe
description: Check Espframe hardware support and identify the correct Guition JC8012P4A1 panel revision before installing.
---

# Choose a Screen for Espframe

Espframe currently supports one display model: the **10.1-inch Guition JC8012P4A1**, an ESP32-P4 touchscreen with a 1280 × 800 landscape panel. Its three hardware revisions need matching firmware. Other ESP32 displays are not currently supported by the ready-to-install firmware.

You need the display, a USB-C data cable for first installation, a desktop computer with Chrome or Edge, an Immich server reachable by the frame, and an [Immich API key](/api-key). The browser installer does not require ESPHome or a local build environment.

## Identify the Hardware Revision

Check ESP32-P4 chip information first. If ESPHome startup logs or `esptool` reports v3.x production silicon, choose V3 regardless of the case marking. If chip information does not identify V3, use the four-digit marking on the rear case to distinguish V1 and V2.

| Firmware profile | How to identify the panel | Setup guide |
| --- | --- | --- |
| V3 — production silicon | ESP32-P4 v3.x | [V3 setup](/screens/jc8012p4a1-v3) |
| V2 — new panel | Case marking `2628` or higher, when the chip is not V3 | [V2 setup](/screens/jc8012p4a1-v2) |
| V1 — original panel | Case marking `2627` or lower, when the chip is not V3 | [V1 setup](/screens/jc8012p4a1-v1) |

For the `esptool` command, panel connections, and revision notes, see the [JC8012P4A1 revision guide](/screens/jc8012p4a1).

## Buy and Mount the Display

| Display | Panel | Stand |
| --- | --- | --- |
| Guition ESP32-P4 10.1-inch `JC8012P4A1`, V1, V2, or V3 | [AliExpress](https://s.click.aliexpress.com/e/_c4LLo3rH) | [MakerWorld](https://makerworld.com/en/models/2490049-guition-p4-10inch-screen-stand#profileId-2736046) |

Confirm the exact model and revision before ordering. The firmware profiles correspond to the JC8012P4A1 hardware and should not be assumed to support other panels with a similar size or processor.

## Install Espframe

After identifying the display, open the [Espframe installer](/install), select the matching firmware profile, and follow the first-time setup steps. If you already have the panel, continue to [Install Espframe](/install).
