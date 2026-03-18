---
title: Firmware Update
description: Over-the-air and HTTP firmware updates from GitHub — auto-update, check frequency, and manual check.
---

# Firmware Update

The device supports **OTA (over-the-air)** and **HTTP-based** firmware updates. It checks a manifest hosted on GitHub Pages for stable and optional beta builds and can auto-update when new versions are available. All controls are in the device web UI at `http://<device-ip>/` under the **Firmware** card (and in Home Assistant under the device’s **Config** entities).

## How updates work

- **Stable** updates use: `https://jtenniswood.github.io/espframe/firmware/manifest.json`
- **Beta** updates use: `https://jtenniswood.github.io/espframe/firmware/beta/manifest.json`

When you run **Check for Update**, the device checks both stable and beta. When auto-update runs on its schedule, it checks the stable manifest; if a new version is available and **Auto Update** is on, it installs automatically. The display may dim briefly during apply; the device reboots after the update.

## Controls

| Control | Type | Default | Description |
|--------|------|---------|-------------|
| **Firmware: Auto Update** | Switch | On | When on, the device automatically checks for updates on the selected frequency and installs them if available. |
| **Firmware: Update Frequency** | Select | Daily | How often to check for updates when Auto Update is on. |
| **Firmware: Version** | Text sensor | *(current)* | Installed firmware version (diagnostic). |
| **Firmware: Check for Update** | Button | — | Run an immediate check for both stable and beta updates. |

### Update Frequency options

| Option | When checks run |
|--------|------------------|
| **Hourly** | Every hour (on the hour) |
| **Daily** | Once per day |
| **Weekly** | Once per week |

Auto-update only runs checks at these intervals; it does not install unless **Auto Update** is on and a new version is found. You can leave Auto Update off and use **Check for Update** when you want to update manually.

::: tip
Use **Check for Update** after a release to get the latest version immediately, or rely on **Auto Update** with **Daily** or **Weekly** for hands-off updates.
:::