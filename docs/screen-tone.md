---
title: Espframe Screen Tone and Night Warmth
description: Adjust Espframe display colour temperature to correct blue cast and automatically warm photos at night.
---

# Espframe Screen Tone and Night Warmth

Adjust display colour temperature and automatic night warmth. All settings are under the **Screen Tone** card in the web UI.

## Mono Mode

Enable **Mono Mode** in **Device → Display → Screen Tone** to show photos in smooth grayscale. It applies to Fit, Fill, and both photos in portrait pairs. The setting defaults to off, survives reboots, and is included in configuration backups.

Changing the toggle reloads the current photo or pair; turning it off restores the original colour. Screen Tone and Night Tone still apply after grayscale conversion, so they can warm monochrome photos.

<!-- ESPFRAME:SETTINGS_TABLE mono_mode START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Mono Mode** | Off | Display photos in smooth grayscale before tone adjustment |
<!-- ESPFRAME:SETTINGS_TABLE mono_mode END -->

## Screen Tone Adjustment

Permanent warm shift to correct display blue cast on the panel. Enable the toggle, drag toward **Warmer** until whites look natural (try 15–25% and compare to a reference). Saved across reboots.

<!-- ESPFRAME:SETTINGS_TABLE screen_tone START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Screen Tone Adjustment** | Off | Enable base colour correction |
| **Intensity** | Cooler (0%) | Warmer = less blue cast |
<!-- ESPFRAME:SETTINGS_TABLE screen_tone END -->

## Night Tone Adjustment

Shifts photos warmer from 60 minutes before sunset through the night, fading back to neutral 60 minutes after sunrise. Sunrise/sunset from your timezone. Stacks on top of Screen Tone (e.g. 15% base + 50% night → 65% at night).

<!-- ESPFRAME:SETTINGS_TABLE night_tone START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Night Tone Adjustment** | Off | Enable sunset/sunrise warm shift |
| **Intensity** | Mid (50%) | Peak warmth strength |
<!-- ESPFRAME:SETTINGS_TABLE night_tone END -->

### Turn On Until Sunrise

Override: force night warm tone on now at full intensity; it turns off after next sunrise, so the override lasts until sunrise. In Home Assistant: **Screen: Warm Tone Override**.

<!-- ESPFRAME:SETTINGS_TABLE warm_tone_override START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Turn On Until Sunrise** | Off | Force night warm tone until the next sunrise |
<!-- ESPFRAME:SETTINGS_TABLE warm_tone_override END -->
