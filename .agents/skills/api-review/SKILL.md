---
name: api-review
description: >-
  Review and reimagine this repository's use of the Immich API. Use when the
  user says "api review" or "/api-review", asks how Espframe could use Immich
  APIs better, wants richer photo filtering, wants faster or higher-quality
  portrait-pair matching, or requests a holistic review of Immich request
  flows, payloads, permissions, and compatibility.
---

# API Review

## Purpose

Review Espframe's Immich integration as a senior API and embedded-systems
engineer. Compare the implementation with the current official Immich API
documentation and propose the best practical design for selecting photos and
finding visually and contextually coherent portrait pairs.

Be bold. The review may recommend different endpoints, request flows, state
models, caching strategies, filter architecture, configuration options, or a
substantial rewrite of the integration when the evidence supports it. Do not
limit the result to small patches around the current design.

Default to research and recommendations only. Do not edit code, change user
behavior, or start a refactor unless the user explicitly asks for
implementation.

## Review Principles

- Use the current official Immich API documentation or official OpenAPI source
  as the authority. Browse it during every review because the API changes over
  time. Use third-party material only to supplement, never replace, official
  evidence.
- Establish which Immich versions Espframe intends to support. Distinguish
  documented behavior, observed repository assumptions, version-specific
  behavior, deprecated behavior, and proposals that require validation.
- Treat API key permissions and least privilege as part of the design. Do not
  recommend broader permissions without explaining why they are needed.
- Optimize for an ESP32-class client: limited heap, response buffers, storage,
  CPU, and network concurrency. Count round trips and consider payload size,
  streaming, pagination, retries, cancellation, and failure recovery.
- Preserve privacy: the frame should continue to communicate directly with the
  user's Immich server unless the user explicitly requests another service.
- Preserve existing settings and behavior through defaults, adapters, or a
  migration plan. New filtering power should not silently change an existing
  slideshow after upgrade.
- Prefer server-side narrowing when the documented API can express it, but
  account for endpoint semantics, selection bias, shared-library behavior, and
  the cost of extra count or probe requests.
- Separate confirmed API opportunities from hypotheses that need a prototype
  or real-library measurement.

## Workflow

### 1. Establish the Current Contract

Inspect the branch, local changes, supported Immich assumptions, user-facing
photo settings, API-key guidance, and validation coverage. Work around local
changes and never revert user work.

Map the complete path from a saved setting to an Immich request, response
parsing, candidate selection, image download, display, retry, and fallback.
Start with these areas, then follow their callers and generated surfaces:

- `common/addon/immich_api.yaml`
- `common/addon/immich_filter.yaml`
- `common/addon/immich_slideshow.yaml`
- `components/espframe/immich_helpers.h`
- `components/espframe/slideshow_component.h`
- `components/espframe/slideshow_controller.h`
- Product-contract checks, configuration generation, docs, and relevant tests

Record every Immich endpoint, method, request field, response field, header,
permission, pagination assumption, retry path, and fallback the product relies
on. Identify duplicated filtering or selection rules across YAML, C++, web
settings, generated assets, documentation, and tests.

### 2. Verify the Official API Surface

Locate and cite the current official documentation for every used or promising
endpoint. At minimum, investigate the documented capabilities around:

- Random and metadata search
- Search statistics and pagination
- Albums and album assets, including shared albums
- People, faces, tags, favorites, dates, asset type, visibility, and archival
  state where supported
- Asset metadata and image delivery parameters
- Memories and other server-curated groupings
- Server version or capability discovery, authentication, and API-key
  permissions

Do not assume similarly named endpoints accept the same filters or return the
same population. Compare schemas and semantics, not just route names. Note
removed, deprecated, unstable, or version-gated options and propose a safe
fallback for each recommendation that depends on them.

### 3. Evaluate Filtering as a Product Capability

Assess both what users can express and how accurately the request pipeline
honors it. Consider:

- Combining sources and filters rather than forcing one mutually exclusive
  source when the API can safely support richer composition
- Inclusion and exclusion rules for albums, people, tags, favorites, date
  ranges, media type, rating, archive state, and orientation when documented
- AND/OR semantics, multiple selected values, empty results, invalid or deleted
  IDs, shared-library assets, and filters that cannot be combined server-side
- Uniform sampling across large libraries, pagination bias, repeated images,
  recent-history exclusion, and whether count-then-random-page flows remain
  correct as the library changes
- Clear UI wording and validation so users can predict the resulting slideshow
- Compatibility of new settings with saved configuration, backup/restore,
  generated web assets, documentation, and API-key permission guidance

Look beyond exposing every server field. Recommend filters that materially
improve a photo-frame experience and explain which tempting options would add
complexity without enough value.

### 4. Reimagine Portrait-Pair Discovery

Trace the current primary-photo and companion-photo workflow, including its
date-window expansion, candidate limits, exclusions, preloading, cancellation,
fallbacks, and "pairs only" behavior.

Evaluate alternatives using documented Immich data and endpoints. A strong
proposal should consider whether candidates can be found and ranked by useful
signals such as capture-time distance, shared album or event, recognized
people, location, orientation and aspect ratio, asset quality, favorites, and
recent display history. Only use signals the API actually exposes at acceptable
cost.

Address both match quality and operational efficiency:

- Avoid pairing an asset with itself or repeatedly showing the same pair.
- Explain candidate-pool construction and scoring/tie-breaking rather than
  relying on arbitrary response order.
- Compare a single broad request, staged search, cached candidate index, and
  other viable architectures by round trips, payload size, heap use, latency,
  and API-version risk.
- Consider whether pairing should happen during source selection, prefetch, or
  display and how that choice affects responsiveness and wasted downloads.
- Define behavior when no strong match exists. A clean single-photo fallback is
  preferable to a visibly poor pair unless the user selected "pairs only."
- Propose measurable quality and performance targets plus representative
  fixtures or library scenarios for evaluation.

### 5. Design the Best Overall Integration

Do not evaluate endpoints in isolation. Look for a coherent API access layer
with clear ownership of request construction, compatibility, parsing,
selection, deduplication, caching, retry policy, and observability.

Consider capability or version negotiation, typed request builders, filtered
stream parsing, bounded caches, request coalescing, cancellation of stale work,
backpressure, and diagnostic counters where they solve demonstrated problems.
Call out API logic that should move out of ESPHome YAML into testable C++ or
generated configuration, while respecting ESPHome constraints.

For major changes, provide a staged path with characterization tests, a first
prototype, compatibility behavior, rollout checks, and rollback points. Be
explicit when the ideal architecture differs substantially from the current
one.

### 6. Validate the Recommendations

Ground every major finding in both repository evidence and official Immich
documentation. Use lightweight local checks when helpful. Do not run a full
firmware compile unless the user asks or implementation work requires it.

Where documentation alone cannot answer a question, specify the smallest safe
experiment against a user-controlled Immich instance. Never require or expose
real API keys, private image metadata, or personal library contents in the
review.

## Output

Lead with a concise thesis describing the highest-value change to the Immich
integration.

Then include:

1. **Current API map** — endpoints, purposes, filters, permissions, request
   costs, parsing strategy, and important fallbacks.
2. **Documentation mismatches and missed capabilities** — each with repository
   evidence, an official source, affected Immich versions, and confidence.
3. **Recommended filtering model** — the user-facing capability and the request
   strategy behind it.
4. **Recommended portrait-pair strategy** — candidate discovery, ranking,
   deduplication, caching or prefetch behavior, fallback, and expected request
   cost.
5. **Ranked recommendations** — for each item state impact, evidence, tradeoffs,
   compatibility, API-version risk, effort (small/medium/large), and the first
   validation step.
6. **Phased roadmap** — quick fixes, a representative prototype, structural
   work, migration, and release validation.
7. **Not recommended** — attractive ideas rejected because the API does not
   support them reliably, they cost too much on-device, or they provide weak
   user value.
8. **Sources and checks** — official links consulted and local checks run or
   deliberately skipped.

Quantify request counts, candidate limits, buffers, or latency when evidence is
available. Otherwise label estimates and state what measurement would confirm
them. If the current design is already optimal in an area, say so plainly.
