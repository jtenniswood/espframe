---
title: USB Flashing Help for Guition ESP32-P4
description: Fix common EspFrame web installer and USB flashing issues on the Guition ESP32-P4 display, including port choice, browser support, and Web Serial.
---

# USB Flashing Help for Guition ESP32-P4

Use this guide when the EspFrame web installer cannot find the display, flashing fails, or the browser does not show a serial port.

## Use the Correct USB-C Port

The Guition ESP32-P4 display has two USB-C ports. For flashing EspFrame, use the **bottom USB-C port** near the edge of the board and next to the USB-A connector. The upper USB-C connector is for the screen ribbon cable and will not work for flashing.

<img src="/usb-plug.png" alt="USB-C cable plugged into the bottom USB-C flashing port on the Guition ESP32-P4 display" style="max-width: 100%; border-radius: 8px; margin: 1rem 0;" />

## Browser Requirements

The [EspFrame installer](/install) uses Web Serial, which means you need:

- Chrome or Edge on a desktop computer.
- A USB-C data cable, not a charge-only cable.
- Permission to access the serial device when the browser asks.

Safari and Firefox do not support the required browser flashing flow.

## If No Serial Device Appears

- Check that the cable is plugged into the bottom USB-C port.
- Try a different USB-C data cable.
- Try another USB port on the computer.
- Close other tools that may already be connected to the device.
- Refresh the install page and click the install button again.

## If Flashing Starts but Fails

- Keep the browser tab open until the install completes.
- Avoid moving the cable or display during flashing.
- Disconnect and reconnect the display, then start the installer again.
- Keep the display connected to USB after flashing. The installer should offer a WiFi setup form in the same browser tab.
- If that form does not appear, or you are setting up without USB, connect your phone or laptop to the setup network named on the display (**espframe_** followed by a device suffix). If the captive portal does not open automatically, visit `http://192.168.4.1` to enter your home WiFi details.

Older firmware uses **espframe** without a suffix. After updating, use the setup name shown on the display whenever you need WiFi recovery. A custom setup SSID in your YAML keeps its configured name.

## After Flashing

When EspFrame boots, connect it to WiFi and open the IP address shown on the display. Enter your Immich server URL and [API key](/api-key), then choose a [photo source](/photo-sources).

For broader setup issues, see [Troubleshooting EspFrame](/troubleshooting).
