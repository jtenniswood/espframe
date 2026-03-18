---
title: Configuration
description: Configure photo sources, slideshow intervals, clock overlay, backlight schedules, and Wi-Fi settings for Espframe for Immich.
---

# Configuration

All settings can be changed at any time through the device's built-in web UI — no recompilation needed.

## Web UI Settings

Open `http://<device-ip>/` in your browser to access the settings page. Changes are saved to flash and persist across reboots.

Settings are grouped in the same order as the device **Settings** menu: Connection, Photo Source, Frequency, Screen Brightness, Screen Tone, Screen Schedule, Clock, and Firmware.

### Connection

| Setting | Description |
|---|---|
| **Immich URL** | Your Immich server address (e.g. `http://192.168.1.30:2283`). Do not include a trailing slash. |
| **Immich API Key** | Your Immich API key (masked in the UI for security) |

See **[API Key](/api-key)** for how to generate a key and which permissions to select.

### Photo Source

| Setting | Default | Description |
|---|---|---|
| **Source** | All Photos | Which photos to display (see [Photo Sources](/photo-sources) for options and setup) |
| **Album IDs** | *(empty)* | When Source is **Album**: comma-separated album UUIDs ([how to find](/photo-sources#album)) |
| **Person IDs** | *(empty)* | When Source is **Person**: comma-separated person UUIDs ([how to find](/photo-sources#person)) |

See **[Photo Sources](/photo-sources)** for options and setup instructions.

### Frequency

| Setting | Default | Description |
|---|---|---|
| **Slideshow Interval** | 15 seconds | Time between photos. Options: 10 s, 15 s, 20 s, 30 s, 45 s, 1 min, 2 min, 3 min, 5 min, 10 min |

### Screen Brightness

| Setting | Default | Description |
|---|---|---|
| **Daytime Brightness** | 100% | Brightness level during the day (10–100%) |
| **Nighttime Brightness** | 75% | Brightness level at night (10–100%) |

Brightness automatically adjusts based on sunrise and sunset times, which are calculated from the selected timezone. The current sunrise and sunset times are displayed below the brightness sliders.

See **[Screen](/screen)** for brightness behaviour and Home Assistant integration.

### Screen Tone

| Setting | Default | Description |
|---|---|---|
| **Screen Tone Adjustment** | Off | Correct the display's blue cast with a permanent warm shift |
| **Night Tone Adjustment** | Off | Automatically warm photos at sunset, neutral at sunrise |
| **Turn on until sunrise** | Off | Force warm tones on immediately until the next sunrise (in Home Assistant this appears as **Screen: Warm Tone Override**) |

See **[Screen Tone](/screen-tone)** for detailed usage and tips.

### Screen Schedule

| Setting | Default | Description |
|---|---|---|
| **Enable Schedule** | Off | Toggle scheduled backlight on/off |
| **On Time** | 6:00 AM | Hour of day the backlight turns on (0–23, e.g. 6 = 6:00 AM) |
| **Off Time** | 11:00 PM | Hour of day the backlight turns off (0–23, e.g. 23 = 11:00 PM) |

When **Enable Schedule** is off, the backlight stays on and follows day/night brightness only; scheduled on/off does not apply. When the schedule is on and the current time is outside the on/off window, the backlight turns off and photo downloads are paused to conserve bandwidth. Downloads resume automatically when the backlight turns back on.

See **[Screen](/screen)** for schedule details and Home Assistant entities.

### Clock

| Setting | Default | Description |
|---|---|---|
| **Show Clock** | On | Toggle the clock overlay on the slideshow screen |
| **Clock Format** | 24 Hour | `24 Hour` or `12 Hour` |
| **Timezone** | Europe/London | Select from a list of common timezones |

::: info
Changes to clock settings can take up to 10 seconds to apply to the screen.
:::

### Firmware

| Setting | Default | Description |
|---|---|---|
| **Auto Update** | On | Automatically check for and install firmware updates |
| **Update Frequency** | Daily | How often to check for updates: `Hourly`, `Daily`, or `Weekly` |

When auto update is enabled, the device periodically checks for new firmware and installs it automatically. You can always check for updates manually using the **Check for Update** button, regardless of the auto update setting.

See **[Firmware Update](/firmware-update)** for how OTA and auto-update work, and for manual check.

---

## Related pages

| Topic | Page |
|-------|------|
| Immich URL and API key | [API Key](/api-key) |
| Albums, people, and photo sources | [Photo Sources](/photo-sources) |
| Brightness, schedule, and display | [Screen](/screen) |
| Colour temperature and warm tone | [Screen Tone](/screen-tone) |
| Touch wake, sleep, and next photo | [Touch Controls](/touch-controls) |
| OTA and auto-update | [Firmware Update](/firmware-update) |

See **[Firmware Update](/firmware-update)** for details on all firmware controls and how updates work.
