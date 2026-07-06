---
title: Phase 4 Release-Proven Architecture
description: Phase 4 status for Espframe's release-confidence checks, expanded browser smoke coverage, compatibility fixtures, and firmware generation guardrails.
---

# Phase 4 Release-Proven Architecture

Phase 4 turns the reset architecture into a release-confidence system. The web UI, backup format, firmware entity names, web endpoints, and Home Assistant names remain unchanged.

## What is now release-proven

- Browser smoke coverage exercises first setup, existing-device settings, photo source modes, date filtering, screen rotation developer safeguards, backup export/import, rejected import behavior, firmware update states, logs, and a mobile-width render check.
- Compatibility fixtures now cover every exported backup group: connection, photos, frequency, firmware updates, clock, and screen.
- Compatibility checks keep backup JSON at version 1 and verify that all product-owned backup fields still map to valid device endpoints.
- Firmware generation checks now verify generated field markers stay in safe entity-field sections, not handwritten lambdas, scripts, actions, or LVGL layout blocks.
- Product-owned firmware setting fields now use an explicit deferred-setting allow-list, which is empty for the current product contract.
- The manual Compile Check workflow builds downloadable factory and OTA firmware artifacts for feature branches, so PR firmware can be tested on a device before merge without publishing it as release firmware.
- The release-readiness command runs the normal local gate and reports whether the repository is clean before publishing. Firmware releases can use the compile-aware variant so ESPHome factory builds are not missed.

## Release checklist

Before publishing a release:

1. Run `npm run check:release-ready-with-compile` before firmware releases.
2. For non-firmware checks where speed matters, run `npm run check:release-ready`.
3. Confirm generated web assets, generated firmware field sections, docs, compatibility fixtures, and release helpers are current.

## What remains future work

Phase 4 does not add product features. Offline storage, VPN support, portrait-specific layouts, onscreen settings, and broader full-device testing remain future phases.
