---
title: Firmware Update
description: Over-the-air and HTTP firmware updates from GitHub — auto-update, check frequency, and manual check.
---

# Firmware Update

OTA and HTTP updates from GitHub. The device checks a manifest on GitHub Pages for stable and beta builds. Manifest: `https://jtenniswood.github.io/espframe/firmware/manifest.json`. Controls: device web UI at `http://<device-ip>/` under **Firmware** (and in Home Assistant).

| Control | Type | Default | Description |
|---------|------|---------|-------------|
| **Auto Update** | Switch | On | Check at selected frequency and install when available |
| **Beta Channel** | Switch | Off | Opt in to pre-release firmware checks |
| **Update Frequency** | Select | Daily | Hourly, Daily, Weekly, or Monthly |
| **Version** | Text sensor | *(current)* | Installed version |
| **Check for Update** | Button | — | Check stable (and beta if opted in); does not install |

**Check for Update** only checks for updates; it does not install. To install, use the **Install** button that appears when a stable or pre-release update is available, or turn on **Auto Update** so the device installs at the selected frequency.

**Beta Channel** must be enabled to check for pre-release firmware. When off, only stable releases are checked — this avoids errors when no pre-release is available.