---
title: Firmware Update
description: Over-the-air and HTTP firmware updates from GitHub — auto-update, check frequency, and manual check.
---

# Firmware Update

OTA and HTTP updates from GitHub. The device checks a manifest on GitHub Pages for stable and beta builds. Manifest: `https://jtenniswood.github.io/espframe/firmware/manifest.json`. Controls: device web UI at `http://<device-ip>/` under **Firmware** (and in Home Assistant).

| Control | Type | Default | Description |
|---------|------|---------|-------------|
| **Auto Update** | Switch | On | Check at selected frequency and install when available |
| **Auto updates** | Select | Daily | Hourly, Daily, Weekly, or Monthly |
| **Version** | Text sensor | *(current)* | Installed version |
| **Check for Update** | Button | — | Check only (stable + pre-release); does not install |

**Check for Update** only checks for updates; it does not install. To install, use the **Install** button that appears when a stable or pre-release update is available, or turn on **Auto Update** so the device installs at the selected frequency. When a pre-release is available, it is shown under the stable update.