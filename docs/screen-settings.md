---
title: EspFrame Screen Brightness and Display Settings
description: Configure EspFrame display controls, including brightness, tone, rotation, clock, and day and night schedules.
---

# EspFrame Screen Brightness and Display Settings

The Device settings page is divided into **Display**, **Sleep & Schedule**, and **System** sections. **Display** includes brightness (day/night), tone, rotation, device language, and clock settings; the NTP servers are in Clock's **Advanced** panel. These controls are available in the web UI and, where applicable, Home Assistant.

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

## Device Language

Choose **Device → Display → Device Language** for English (`en`, the default),
German (`de`), French (`fr`), Spanish (`es`), Italian (`it`), Dutch (`nl`), or
Portuguese (`pt`, Portugal). Home Assistant exposes the saved selection as **Device: Language**.
The setting survives restarts and is included in configuration backups; restoring
an older backup without a language leaves the current selection unchanged.

The language changes built-in setup text, loading and error messages, month names,
and relative photo ages immediately. Immich location names retain their original
text. The web interface stays in English.

To improve a translation or add a language, see the
[translation contribution guide](/translations).

<!-- ESPFRAME:SETTINGS_TABLE device_language START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Device Language** | en | Translate built-in display text and photo dates: English (`en`), German (`de`), French (`fr`), Spanish (`es`), Italian (`it`), Dutch (`nl`), or Portuguese (`pt`). The web interface stays in English. |
<!-- ESPFRAME:SETTINGS_TABLE device_language END -->

## Frame Name

Open **Device → System → Frame Name** to give a frame a recognizable name, such
as `Living Room`. Check the live network address preview, then click **Save & Restart**.
The confirmation dialog links to the new hostname and current IP. The web title and backup filename use the saved
name immediately; the network hostname and Home Assistant friendly name apply
on restart. Follow the displayed address to reconnect, or use the frame's IP.

Hostnames use the first 19 characters of a simplified name plus the last four
MAC characters, for example `living-room-b2c3.local`. Names without Latin letters
or digits use `frame` as the hostname prefix. The displayed name supports Unicode
up to 120 UTF-8 bytes. Home Assistant names you assigned manually take precedence.

Existing firmware names stay unchanged until you save a name. Leave the field
blank and choose **Save & Restart** to restore the firmware defaults. Saved names survive normal OTA
updates and power cycles. If storage is full, a save reports an error and retains
the previous name; it never clears other settings to make room.

## Clock

Set your preferred clock format and timezone during setup or in **Device → Display → Clock**. The timezone also controls sunrise/sunset based brightness and night tone.

::: details Clock defaults and time servers
<!-- ESPFRAME:SETTINGS_TABLE clock START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Format** | 24 Hour | Choose whether the on-screen clock uses a 24-hour or 12-hour format. |
<!-- ESPFRAME:SETTINGS_TABLE clock END -->

The setup wizard defaults to **Europe/London (GMT+0)** timezone, and shows the clock by default. The clock refreshes every **60 seconds**. Time sync uses **0.pool.ntp.org**, **1.pool.ntp.org**, and **2.pool.ntp.org**; change these in Clock's **Advanced** panel if needed.
:::
