---
title: Espframe Firmware Updates
description: Check, install, automate, and roll back Espframe and Wi-Fi firmware updates from the device web interface.
---

# Espframe Firmware Updates

OTA and HTTP updates come from GitHub Pages. The original panel checks `https://jtenniswood.github.io/espframe/firmware/manifest.json`; the new panel checks `https://jtenniswood.github.io/espframe/firmware/jc8012p4a1-v2/manifest.json`. Open the device web UI at `http://<device-ip>/`, choose **Device**, then expand **Firmware** in the **System** section to manage display and Wi-Fi firmware. The same update entities remain available in Home Assistant.

During OTA updates, Espframe keeps the current backlight state while the update starts. If Home Assistant or the screen sleep control had already turned the display off, the screen stays off after the update reboot until it is woken again.

<!-- ESPFRAME:SETTINGS_TABLE firmware_controls START -->
| Control | Type | Default | Description |
|---------|------|---------|-------------|
| **Auto Update** | Switch | On | Check at selected frequency and install when available |
| **Update Frequency** | Select | Daily | Hourly, Daily, Weekly, or Monthly |
| **WiFi Firmware Auto Update** | Switch | On | Check daily and install compatible ESP32-C6 WiFi firmware automatically |
<!-- ESPFRAME:SETTINGS_TABLE firmware_controls END -->

| Status or action | Type | Description |
|------------------|------|-------------|
| **Version** | Text sensor | Installed version |
| **Check for Update** | Button | Check stable firmware; does not install |

**Check for Update** only checks for updates; it does not install. To install, use the **Install Update** button that appears when a stable update is available, or turn on **Auto Update** so the device installs at the selected frequency.

The **Previous firmware** panel lists up to four earlier stable releases. Select a version and confirm the installation to roll back. The browser downloads that release, uploads it directly to the display, and waits for the display to restart. Do not remove power during an update.


## ESP32-C6 Wi-Fi Coprocessor Updates

The 10-inch ESP32-P4 frame also exposes separate controls in the **WiFi firmware** panel inside the grouped Firmware card. These use ESPHome's hosted ESP32-C6 firmware manifest, not the Espframe display firmware manifest.

| Status or action | Type | Description |
|------------------|------|-------------|
| **ESP32-C6: Update Available** | Text sensor | Shows whether the coprocessor firmware is up to date |
| **ESP32-C6: Current Firmware** | Text sensor | Installed ESP32-C6 firmware version |
| **ESP32-C6: Available Firmware** | Text sensor | Latest compatible ESP32-C6 firmware version |
| **WiFi Firmware: Auto Update** | Switch | Automatically install compatible updates; on by default |
| **Firmware ESP32-C6: Check for Update** | Button | Check for a coprocessor firmware update |
| **Firmware ESP32-C6: Install Update** | Button | Install the available coprocessor firmware update |

Automatic Wi-Fi firmware checks run every 24 hours. Turning automatic updates back on also starts an immediate check.
