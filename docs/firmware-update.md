---
title: Firmware Update
description: Over-the-air and HTTP firmware updates from GitHub — auto-update, check frequency, and manual check.
---

# Firmware Update

OTA and HTTP updates from GitHub. The device checks a manifest on GitHub Pages for stable and beta builds. Manifest: `https://jtenniswood.github.io/espframe/firmware/manifest.json`. Controls: device web UI at `http://<device-ip>/` under **Firmware** (and in Home Assistant).

| Control | Type | Default | Description |
|---------|------|---------|-------------|
| **Auto Update** | Switch | On | Check at selected frequency and install when available |
| **Update Frequency** | Select | Daily | Hourly, Daily, or Weekly |
| **Version** | Text sensor | *(current)* | Installed version |
| **Check for Update** | Button | — | Immediate check (stable + beta) |

Leave Auto Update off and use **Check for Update** for manual updates.