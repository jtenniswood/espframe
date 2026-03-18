---
title: Touch Controls
description: Use the frame’s touchscreen to wake or sleep the display and advance the slideshow.
---

# Touch Controls

The frame uses a **GSL3680** capacitive touchscreen. On the slideshow screen you can wake the display, turn it off with a timed hold, and advance to the next photo.

## Gestures

| Gesture | When backlight is **off** | When backlight is **on** |
|--------|----------------------------|---------------------------|
| **Tap** | Turns the backlight on (wake). If a [screen schedule](/screen#screen-schedule) is enabled and the current time is outside the on/off window, tap does nothing. | Starts a 3-second hold timer. |
| **Release** | — | If you release before 3 seconds, the timer is cancelled and the backlight stays on. |
| **Hold 3 seconds** | — | Turns the backlight off (manual sleep). The display fades out over 500 ms and photo downloads pause until you tap to wake. |
| **Long press** | — | Advances to the **next** photo in the slideshow. |

So in practice:

- **Wake:** Tap the screen when it’s off.
- **Sleep:** Press and hold for 3 seconds, then release.
- **Next photo:** Long-press on the photo (without holding long enough to trigger sleep).

## Technical notes

- Touch is handled by LVGL widgets on the slideshow and portrait-pair views (`on_press`, `on_long_press`, `on_release`). The same gestures apply whether a single image or a portrait pair is shown.
- The 3-second hold uses a restarting script: each new touch restarts the timer; releasing stops it. Only a full 3-second hold turns the backlight off.
- After manual sleep, wake restores day/night brightness and resumes prefetch. If the screen schedule is on and the time is outside the schedule window, a tap will not turn the backlight on until the schedule allows it.

## Related

| Topic | Page |
|-------|------|
| Brightness and schedule | [Screen](/screen) |
| Slideshow timing | [Configuration](/configuration) |
| All settings | [Configuration](/configuration) |
