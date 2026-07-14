---
title: Product Metadata Foundation
description: Phase 1 status for Espframe's product metadata source of truth, generated outputs, validation gates, and Phase 2 boundaries.
---

# Product Metadata Foundation

Phase 1 of the reset is the product metadata foundation. The goal is to keep the current user experience unchanged while making the important product behavior visible in one place and checked before release.

## Product-owned behavior

The files under `product/contract/` are the source of truth for the stable product contract. They separate project and release policy, device profiles, settings, the contract version, and the standard JSON Schema used to validate them.

`product/espframe.json` remains available as a generated compatibility file for existing tools while they migrate to the split contract.

- project identity, public docs URLs, support links, license, repository, and release metadata
- device identity, hardware details, build entrypoints, package includes, substitutions, pins, and workflow references
- setting keys, defaults, options, firmware ownership, documentation ownership, and generated docs table membership
- web UI entity metadata, manual endpoints, aliases, startup fetch behavior, live refresh behavior, local state keys, and log retention
- backup export/import shape, backup compatibility fixtures, and user-facing backup behavior
- GitHub workflow names, events, permissions, release jobs, docs deployment wiring, Node version, Docker compile settings, and firmware release contracts

## Generated outputs

Phase 1 safely generates or verifies the parts that can be derived from product metadata without redesigning the application:

- web settings metadata, endpoint registration, state registration, manual/static entities, aliases, startup fetch keys, live render metadata, and firmware manifest URLs
- settings documentation tables between `ESPFRAME:SETTINGS_TABLE` markers
- timezone assets, public webserver bundle, and generated style output freshness

## Validation gates

The required Phase 1 release gates are:

- `npm run check:schema`
- `npm run check:product`
- `npm run check:all`

Together these validate product schema, firmware YAML references, generated web metadata, generated docs tables, backup fixtures, release helpers, GitHub workflows, Node/Docker settings, and docs build output.

## Phase 2 boundary

The remaining hard-coded web and firmware behavior is intentionally left for Phase 2 when it would require structural work rather than safe metadata ownership. That includes broader firmware generation, web UI component restructuring, richer compatibility testing around saved settings and upgrades, and deeper cleanup of handwritten UI flow logic.
