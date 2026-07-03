---
title: Espframe Firmware Updates
description: Configure Espframe over-the-air and HTTP firmware updates from GitHub, including auto-update, check frequency, and manual checks.
---

# Espframe Firmware Updates

OTA and HTTP updates from GitHub. The device checks the stable firmware manifest on GitHub Pages. The 10-inch model uses `https://jtenniswood.github.io/espframe/firmware/manifest.json`. Controls: device web UI at `http://<device-ip>/` under **Firmware** (and in Home Assistant).

During OTA updates, Espframe keeps the current backlight state while the update starts. If Home Assistant or the screen sleep control had already turned the display off, the screen stays off after the update reboot until it is woken again.

<!-- ESPFRAME:SETTINGS_TABLE firmware_controls START -->
| Control | Type | Default | Description |
|---------|------|---------|-------------|
| **Auto Update** | Switch | On | Check at selected frequency and install when available |
| **Update Frequency** | Select | Daily | Hourly, Daily, Weekly, or Monthly |
| **Stable Manifest URL** | Text | Device default | Advanced: custom stable update manifest |
<!-- ESPFRAME:SETTINGS_TABLE firmware_controls END -->

| Status or action | Type | Description |
|------------------|------|-------------|
| **Version** | Text sensor | Installed version |
| **Check for Update** | Button | Check stable firmware; does not install |

**Check for Update** only checks for updates; it does not install. To install, use the **Install** button that appears when a stable update is available, or turn on **Auto Update** so the device installs at the selected frequency.

## ESP32-C6 Wi-Fi Coprocessor Updates

The 10-inch ESP32-P4 frame also exposes separate Home Assistant controls for the ESP32-C6 Wi-Fi coprocessor firmware. These use ESPHome's hosted ESP32-C6 firmware manifest, not the Espframe display firmware manifest.

| Status or action | Type | Description |
|------------------|------|-------------|
| **ESP32-C6: Update Available** | Text sensor | Shows whether the coprocessor firmware is up to date |
| **ESP32-C6: Current Firmware** | Text sensor | Installed ESP32-C6 firmware version |
| **ESP32-C6: Latest Firmware** | Text sensor | Latest compatible ESP32-C6 firmware version |
| **Firmware ESP32-C6: Check for Update** | Button | Check for a coprocessor firmware update |
| **Firmware ESP32-C6: Install Update** | Button | Install the available coprocessor firmware update |

Advanced users can point the stable manifest URL at another ESP-Web-Tools style manifest. Use a full http:// or https:// URL. Firmware downloads then follow the paths declared by that manifest.
