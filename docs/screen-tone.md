---
title: EspFrame Screen Tone and Night Warmth
description: Adjust EspFrame display colour temperature to correct blue cast and automatically warm photos at night.
---

# EspFrame Screen Tone and Night Warmth

Adjust display colour temperature and automatic night warmth. All settings are under the **Screen Tone** card in the web UI.

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
