---
title: Current Reset Architecture
description: How Espframe now separates its product contract, offline control panel, firmware state, source ownership, and release safeguards.
---

# Current reset architecture

This is the current technical shape of Espframe after rebuilding its main
boundaries around the existing product experience. Users keep the same device,
settings, backup format, Home Assistant entities, and slideshow behaviour.
The difference is that the parts underneath now have clear owners and checked
interfaces.

## The five boundaries

### 1. One versioned product contract

The schema-validated files in `product/contract/` define the stable product
surface. They describe settings, devices, generated assets, compatibility
requirements, and the firmware API contract. Generated firmware and web assets
are checked against this source before a pull request can pass.

### 2. One device configuration API

The firmware exposes versioned capabilities and configuration endpoints under
`/espframe/api/v1/`. The web panel reads one configuration snapshot and applies
related changes atomically, so a partially failed save cannot leave a group of
settings half updated. Existing individual endpoints remain available for
older clients and upgrades.

### 3. An embedded, typed control panel

The editable web panel lives in `docs/webserver/src/` and is type-checked before
being bundled into the firmware. Both normal and factory firmware carry the
same local JavaScript and CSS. Opening the panel therefore does not depend on a
hosted Espframe site, public fonts, placeholder images, or another web service.

### 4. Firmware components own runtime state

Immich request retries, cooldowns, failure windows, memory searches, and
pagination live in one C++ request-state object. Slideshow slots, navigation,
preloading, display selection, queues, and diagnostics live in the slideshow
component. YAML remains the device wiring and automation layer instead of being
the owner of many loosely related global variables.

### 5. Releases enforce their limits

The ownership manifest records authored, generated, vendored, and externally
fetched build sources. The runtime network policy prevents accidental public
web dependencies. Size budgets guard the embedded web bundle, firmware flash,
RAM, factory binary, and OTA binary. Pull-request and release workflows build
both firmware forms and reject a change that exceeds those limits.

## Change flow

1. Edit the product contract, typed web source, authored firmware component,
   or device YAML that owns the behaviour.
2. Regenerate derived assets with `npm run generate` when required.
3. Run `npm run check:pr` for contracts, compatibility, tests, and docs.
4. Compile factory and OTA firmware. The build-budget check reads the real
   ESPHome size report and binary files.
5. Use the manual **PR Validation** workflow to produce test firmware for a
   feature branch without publishing or changing `main`.

## Deliberate compatibility choices

- The backup format remains version 1.
- Existing Home Assistant names and legacy web endpoints remain supported.
- The current visual design and user journeys are preserved.
- Factory and OTA firmware use the same product contract and embedded panel.

Future features such as offline photo storage, VPN support, portrait-specific
layouts, and onscreen settings can now be added behind these boundaries without
putting today’s upgrade path at risk.
