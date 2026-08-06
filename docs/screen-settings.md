---
title: Espframe Screen Brightness and Display Settings
description: Configure Espframe display controls, including brightness, tone, rotation, clock, and day and night schedules.
---

# Espframe Screen Brightness and Display Settings

The Device settings page is divided into **Display**, **Sleep & Schedule**, and **System** sections. **Display** includes brightness (day/night), tone, rotation, and clock settings; the NTP servers are in Clock's **Advanced** panel. These controls are available in the web UI and, where applicable, Home Assistant.

## Screen Brightness

**Screen Brightness** sets day and night levels; the frame switches by sunrise/sunset from your timezone. Sunrise/sunset shown below the sliders. In HA: **Screen: Backlight** (on/off + brightness).

<!-- ESPFRAME:SETTINGS_TABLE screen_brightness START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Daytime Brightness** | 100% | Day (10-100%) |
| **Nighttime Brightness** | 75% | Night (10-100%) |
<!-- ESPFRAME:SETTINGS_TABLE screen_brightness END -->

## Night Schedule

**Night Schedule** turns the display fully off outside a time window: it switches to a black page, pauses LVGL, turns the backlight light off, and forces the physical PWM output off. When **Schedule Screen Off** is off, only day/night brightness applies. On/Off are hour-of-day (0–23). In HA: **Screen: Schedule Enabled**, **Screen: Schedule On Hour**, **Screen: Schedule Off Hour**, **Screen: Schedule Wake Timeout**.

<!-- ESPFRAME:SETTINGS_TABLE night_schedule START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Schedule Screen Off** | Off | Use scheduled on/off |
| **On Time** | 6 | Backlight on (hour) |
| **Off Time** | 23 | Backlight off (hour) |
| **When Woken, Idle Time To Screen Off** | 60 seconds | How long a touch wake stays on during the off period |
<!-- ESPFRAME:SETTINGS_TABLE night_schedule END -->

In Home Assistant, **Screen: Sleep** and **Screen: Wake** expose the same sleep/wake behavior as the touchscreen controls. Sleep pauses the slideshow fetch loop instead of only dimming the panel.

## Rotation

**Rotation** rotates the LVGL display layer, so the picture and touch input turn together. This uses ESPHome 2026.4's LVGL rotation support.

The setting only exposes normal and upside-down orientations. On the 10" model, the firmware keeps its internal 90-degree panel offset and maps these two choices onto the correct LVGL values.

90 and 270 degree rotations are hidden unless Developer Features is enabled. Turning Developer Features off resets portrait rotation back to 0 degrees.

<!-- ESPFRAME:SETTINGS_TABLE screen_rotation START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Rotation** | 0 degrees | Rotate the screen to 0 or 180 degrees. |
<!-- ESPFRAME:SETTINGS_TABLE screen_rotation END -->
