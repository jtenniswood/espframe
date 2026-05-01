---
title: Espframe Screen Tone and Night Warmth
description: Adjust Espframe display colour temperature to correct blue cast and automatically warm photos at night.
---

# Espframe Screen Tone and Night Warmth

Adjust display colour temperature and automatic night warmth. All settings are under the **Screen Tone** card in the web UI.

## Screen Tone Adjustment

Permanent warm shift to correct the panel’s blue tint. Enable the toggle, drag toward **Warmer** until whites look natural (try 15–25% and compare to a reference). Saved across reboots.

| Setting | Default | Description |
|---------|---------|-------------|
| **Screen Tone Adjustment** | Off | Enable base colour correction |
| **Intensity** | Cooler (0%) | Warmer = less blue cast |

## Night Tone Adjustment

Shifts photos warmer from ~60 min before sunset through the night, fading back after sunrise. Sunrise/sunset from your timezone. Stacks on top of Screen Tone (e.g. 15% base + 50% night → 65% at night).

| Setting | Default | Description |
|---------|---------|-------------|
| **Night Tone Adjustment** | Off | Enable sunset/sunrise warm shift |
| **Intensity** | Mid (50%) | Peak warmth strength |

### Turn On Until Sunrise

Override: force night warm tone on now at full intensity; it turns off after next sunrise. In Home Assistant: **Screen: Warm Tone Override**.
