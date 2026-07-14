---
title: Phase 3 Reset Architecture
description: Phase 3 status for Espframe's modular web source, generated firmware setting fields, compatibility fixtures, and remaining future work.
---

# Phase 3 Reset Architecture

Phase 3 established the first reset architecture by making product metadata and compatibility gates part of normal project structure. Later reset work split that metadata into the schema-validated `product/contract/` boundary while preserving the user experience, backup format, firmware entity names, web endpoints, and Home Assistant names.

## What is product-owned

The product contract now owns the stable settings contract used across the web UI, docs, compatibility checks, and generated firmware field sections.

- Web settings metadata, entity mappings, aliases, startup fetch keys, live state keys, tabs, support links, and firmware manifest URLs are generated from product metadata.
- Firmware setting fields inside marked YAML sections are generated for safe repeated fields: select options/defaults, number ranges/defaults, text field shape/defaults, and switch restore defaults.
- Backup compatibility fixtures cover full and partial v1 configs, old saved settings, future unknown fields, missing groups, developer-only values, malformed noncritical values, invalid photo IDs, and invalid firmware manifest URLs.
- Release gates check generated assets, product contracts, backup compatibility, generated firmware fields, web modules, browser smoke coverage, docs, release helpers, and firmware release contracts.

## What was modularized

The shipped web app is still one generated file at `docs/public/webserver/app.js`, but its source is split into focused modules under `docs/webserver/src/`.

- App shell and tab layout
- Endpoint registration
- Runtime state and live updates
- Startup wizard
- Settings controls
- Shared live/UI helpers
- Backup import/export
- Compatibility helpers

This keeps the browser payload simple while making the source easier to review and test.

## What remains future work

Phase 3 intentionally avoids feature redesign. These items remain outside the reset completion work:

- Broader browser automation beyond the current Chrome smoke coverage
- Broader firmware generation for scripts, actions, lambdas, LVGL layout, and full YAML files
- Deeper compatibility migrations if the backup JSON format ever moves beyond version 1
- New product features such as offline storage, VPN, portrait layouts, or onscreen settings

The current release baseline is `npm run check:all`; ESPHome compile checks should also be run before firmware releases when Docker is available.
