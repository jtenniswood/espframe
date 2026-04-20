---
title: Screen Settings
description: Screen brightness, tone, and schedule — the display controls available in the device Settings menu.
---

# Screen Settings

Display controls in **Settings**: orientation, brightness (day/night), tone, and optional schedule. Available in the web UI and (where applicable) Home Assistant.

## Screen Orientation

**Screen Orientation** rotates the LVGL display layer, so the picture and touch input turn together. This uses ESPHome 2026.4's LVGL rotation support.

The setting only exposes normal and upside-down orientations. On the 10" model, the firmware keeps its internal 90-degree panel offset and maps these two choices onto the correct LVGL values.

| Setting | Default | Description |
|---------|---------|-------------|
| **Rotation** | 0 degrees | Rotate the screen to 0 or 180 degrees. |

## Screen Brightness

**Screen Brightness** sets day and night levels; the frame switches by sunrise/sunset from your timezone. Sunrise/sunset shown below the sliders. In HA: **Screen: Backlight** (on/off + brightness).

| Setting | Default | Description |
|---------|---------|-------------|
| **Daytime Brightness** | 100% | Day (10–100%) |
| **Nighttime Brightness** | 75% | Night (10–100%) |

## Screen Schedule

**Screen Schedule** turns the backlight off outside a time window and pauses downloads. When **Enable Schedule** is off, only day/night brightness applies. On/Off are hour-of-day (0–23). In HA: **Screen: Schedule**, **Schedule On**, **Schedule Off**.

| Setting | Default | Description |
|---------|---------|-------------|
| **Enable Schedule** | Off | Use scheduled on/off |
| **On Time** | 6 | Backlight on (hour) |
| **Off Time** | 23 | Backlight off (hour) |
