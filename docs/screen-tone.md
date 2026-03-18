---
title: Screen Tone
description: Adjust display colour temperature to correct blue cast and automatically warm photos at night.
---

# Screen Tone

The Screen Tone feature lets you adjust the colour temperature of displayed photos. It can correct the display's cool/blue cast and automatically shift to warmer tones in the evening, mimicking how natural light changes throughout the day.

All settings are found under the **Screen Tone** card in the web UI.

## Screen Tone Adjustment

A permanent warm shift applied to every photo to compensate for the display panel's blue tint. Enable the toggle, then drag the slider toward **Warmer** until whites and skin tones look natural. The setting is saved across reboots.

| Setting | Default | Description |
|---|---|---|
| **Screen Tone Adjustment** | Off | Enable base colour correction |
| **Intensity** | Cooler (0%) | Drag toward Warmer to reduce blue cast |

::: tip
Start around 15–25% and compare against a printed photo or your phone screen to judge when whites look neutral.
:::

## Night Tone Adjustment

Automatically shifts photos toward warm tones as sunset approaches, stays warm through the night, and fades back to neutral after sunrise. This replicates the "golden hour" effect and makes the frame feel less like a glowing screen in a dimly lit room.

The warmth ramps gradually over the 60 minutes before sunset and the 60 minutes after sunrise, so transitions are smooth and natural. Sunrise and sunset times are calculated from your selected timezone.

| Setting | Default | Description |
|---|---|---|
| **Night Tone Adjustment** | Off | Enable automatic sunset/sunrise warm shift |
| **Intensity** | Mid (50%) | How strong the warm shift is at its peak |

The night tone is additive — it stacks on top of the Screen Tone Adjustment, so if you have a base correction of 15% and night intensity at 50%, photos will be at 15% warmth during the day and 65% in the evening.

### Turn On Until Sunrise

A manual override that forces the night warm tone on immediately at full intensity, regardless of the time of day. It automatically turns itself off after sunrise the next morning.

Use this when you want the warm tone effect right now without waiting for sunset — for example, if the room lights are dim in the afternoon or you just want the warmer look.

## How It Works

The tint is applied directly to each photo's pixel data after it loads, using precomputed lookup tables for each RGB channel. This means:

- The effect applies to both the photo content and the accent-coloured letterbox bars
- Each new photo gets the correct tone applied automatically
- Changing a slider updates the current photo immediately — no need to wait for the next slide
- There is no visible performance impact; the lookup table approach processes the full image buffer in under 50ms
