---
title: Espframe Screen Brightness and Display Settings
description: Configure Espframe screen brightness, day and night schedules, display tone, and rotation from the device settings menu.
---

# Espframe Screen Brightness and Display Settings

Display controls in **Settings**: brightness (day/night), tone, optional schedule, and rotation. Available in the web UI and (where applicable) Home Assistant.

## Screen Brightness

**Screen Brightness** sets day and night levels; the frame switches by sunrise/sunset from your timezone. Sunrise/sunset shown below the sliders. In HA: **Screen: Backlight** (on/off + brightness).

| Setting | Default | Description |
|---------|---------|-------------|
| **Daytime Brightness** | 100% | Day (10–100%) |
| **Nighttime Brightness** | 75% | Night (10–100%) |

## Night Schedule

**Night Schedule** turns the display fully off outside a time window: it switches to a black page, pauses LVGL, turns the backlight light off, and forces the physical PWM output off. When **Schedule Screen Off** is off, only day/night brightness applies. On/Off are hour-of-day (0–23). In HA: **Screen: Schedule Enabled**, **Screen: Schedule On Hour**, **Screen: Schedule Off Hour**, **Screen: Schedule Wake Timeout**.

| Setting | Default | Description |
|---------|---------|-------------|
| **Schedule Screen Off** | Off | Use scheduled on/off |
| **On Time** | 6 | Backlight on (hour) |
| **Off Time** | 23 | Backlight off (hour) |
| **When Woken, Idle Time To Screen Off** | 60 seconds | How long a touch wake stays on during the off period |

## Rotation

**Rotation** rotates the LVGL display layer, so the picture and touch input turn together. This uses ESPHome 2026.4's LVGL rotation support.

The setting only exposes normal and upside-down orientations. On the 10" model, the firmware keeps its internal 90-degree panel offset and maps these two choices onto the correct LVGL values.

| Setting | Default | Description |
|---------|---------|-------------|
| **Rotation** | 0 degrees | Rotate the screen to 0 or 180 degrees. |
