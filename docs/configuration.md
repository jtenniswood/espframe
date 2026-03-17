---
title: Configuration
description: Configure slideshow intervals, clock overlay, backlight schedules, and Wi-Fi settings for your Immich Frame.
---

# Configuration

All settings can be changed at any time through the device's built-in web UI — no recompilation needed.

## Web UI Settings

Open `http://<device-ip>/` in your browser to access the settings page. Changes are saved to flash and persist across reboots.

### Immich Connection

| Setting | Description |
|---|---|
| **Immich URL** | Your Immich server address (e.g. `http://192.168.1.30:2283`). Do not include a trailing slash. |
| **Immich API Key** | Your Immich API key (masked in the UI for security) |

See [Creating an API Key](/api-key) for how to generate a key and which permissions to select.

### Frequency

| Setting | Default | Description |
|---|---|---|
| **Slideshow Interval** | 15s | Time between photos (5–300 seconds) |

### Screen Schedule

| Setting | Default | Description |
|---|---|---|
| **Enable Schedule** | Off | Toggle scheduled backlight on/off |
| **On Time** | 6:00 AM | Hour the backlight turns on |
| **Off Time** | 11:00 PM | Hour the backlight turns off |

When the schedule is active and the current time is outside the on/off window, the backlight turns off and photo downloads are paused to conserve bandwidth. Downloads resume automatically when the backlight turns back on.

### Screen Brightness

| Setting | Default | Description |
|---|---|---|
| **Daytime Brightness** | 100% | Brightness level during the day (10–100%) |
| **Nighttime Brightness** | 75% | Brightness level at night (10–100%) |

Brightness automatically adjusts based on sunrise and sunset times, which are calculated from the selected timezone. The current sunrise and sunset times are displayed below the brightness sliders.

### Clock

| Setting | Default | Description |
|---|---|---|
| **Show Clock** | On | Toggle the clock overlay on the slideshow screen |
| **Clock Format** | 24 Hour | `24 Hour` or `12 Hour` |
| **Timezone** | Europe/London | Select from a list of common timezones |

!!! note
    Changes to clock settings can take up to 10 seconds to apply to the screen.

### Firmware Updates

| Setting | Default | Description |
|---|---|---|
| **Auto Update** | On | Automatically check for and install firmware updates |
| **Update Frequency** | Daily | How often to check for updates: `Hourly`, `Daily`, or `Weekly` |

When auto update is enabled, the device periodically checks for new firmware and installs it automatically. You can always check for updates manually using the **Check for Update** button, regardless of the auto update setting.
