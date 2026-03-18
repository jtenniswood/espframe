---
title: Screen Settings
description: Screen brightness, tone, and schedule — the display controls available in the device Settings menu.
---

# Screen

In the device **Settings** menu you’ll see three display-related sections:

- **Screen Brightness** — Day and night brightness with automatic sunrise/sunset switching
- **Screen Schedule** — Optional backlight on/off times and download pausing

All of these are available in the built-in web UI and (where applicable) as Home Assistant entities.

## Screen Brightness

The **Screen Brightness** card lets you set separate brightness levels for day and night. The frame switches between them automatically using sunrise and sunset times from your configured timezone.

| Setting | Default | Description |
|---|---|---|
| **Daytime Brightness** | 100% | Brightness during the day (10–100%) |
| **Nighttime Brightness** | 75% | Brightness at night (10–100%) |

Sunrise and sunset are shown below the sliders so you can see when the switch happens. In Home Assistant, the **Screen: Backlight** light entity controls on/off and current brightness.

## Screen Schedule

**Screen Schedule** optionally turns the backlight off outside a time window and pauses photo downloads to save power and bandwidth.

| Setting | Default | Description |
|---|---|---|
| **Enable Schedule** | Off | Turn on scheduled backlight on/off |
| **On Time** | 6:00 AM | Hour of day the backlight turns on (0–23) |
| **Off Time** | 11:00 PM | Hour of day the backlight turns off (0–23) |

- When **Enable Schedule** is **off**, the backlight stays on and only day/night brightness applies.
- When the schedule is **on** and the current time is **outside** the on/off window, the backlight turns off and downloads pause. They resume when the schedule turns the backlight back on.

On and Off times are hour-of-day (e.g. 6 = 6:00 AM, 23 = 11:00 PM). In Home Assistant you’ll see **Screen: Schedule**, **Screen: Schedule On**, and **Screen: Schedule Off**.