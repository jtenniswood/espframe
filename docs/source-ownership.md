---
title: Source ownership and dependencies
description: Which Espframe files are authored, generated, vendored, or fetched while building.
---

# Source ownership and dependencies

Espframe records its source boundaries in `product/source-ownership.json` and
checks them on every pull request. This avoids accidental edits to generated
files and makes bundled third-party code explicit.

## The four ownership classes

- **Authored** files are maintained directly in this repository.
- **Generated** files are rebuilt from their recorded source files with
  `npm run generate`; they should not be edited by hand.
- **Vendored** components are local copies of upstream projects needed by the
  firmware. Each entry records its upstream revision, license, and the reason
  the local copy differs.
- **External build assets** are downloaded by ESPHome while compiling. They
  are not runtime dependencies of the finished device.

## Runtime network boundary

The device web control panel, fonts, styles, scripts, and initial image state
do not require public hosting. Before a real Immich photo URL is assigned,
`remote_image` uses a loopback-only URL. Normal runtime network access is
limited to services the user configured, plus the versioned Espframe firmware
update manifest and assets.

The machine-readable manifest is authoritative; update it whenever a source
root, generated output, vendored component, or build-time asset changes.
