---
title: ESP32-C6 Wi-Fi Recovery
description: Update and diagnose the ESP32-C6 Wi-Fi coprocessor used by the Espframe P4 display.
---

# ESP32-C6 Wi-Fi Recovery

The Guition ESP32-P4 display uses a separate ESP32-C6 coprocessor for Wi-Fi. A stale or incompatible C6 firmware can cause repeated disconnects, failed Wi-Fi setup, or C6 update timeouts.

## Update the C6 firmware

If the frame is online and its web interface is available:

1. Open the frame's web interface and go to **Device → System → Firmware → WiFi Firmware**.
2. Check **ESP32-C6: Current Firmware** and **ESP32-C6: Update Available**.
3. Choose **Firmware ESP32-C6: Check for Update**. This checks for an update but does not install it.
4. If an update is available, choose **Firmware ESP32-C6: Install Update** and keep the frame powered until it restarts.
5. Confirm that the frame reconnects to Wi-Fi. Keep **WiFi Firmware: Auto Update** enabled so compatible updates can install automatically.

The C6 updater downloads firmware through the working network connection. It cannot repair a C6 that is too damaged to connect to the P4 or Wi-Fi. Espframe does not currently publish a separate offline C6 recovery installer, so do not erase the frame or flash a different panel revision as a recovery attempt.

## If the update does not work

1. Restart the frame and wait for it to finish booting.
2. If the frame reconnects, check and install the C6 update again.
3. If the frame is still available over USB, [collect a startup log](/collect-usb-logs), including the C6 error and the first 60–90 seconds after restart.
4. Open an [Espframe GitHub issue](https://github.com/jtenniswood/espframe/issues/new) with the panel revision, current frame firmware version, C6 status, symptoms, and reviewed USB log.

Avoid attempting direct UART recovery unless you have the board-specific pinout and a suitable 3.3 V USB-to-UART adapter. Incorrect wiring can damage the display. See [Firmware Updates](/firmware-update) for the normal update controls.
