---
name: expert-reset
description: >-
  Reimagine this repository from first principles using the current product and
  user experience as the target. Use when the user says "/expert-reset", asks
  how the project should be rebuilt if starting again, wants bold architecture
  or component-structure recommendations, or wants a research-only reset plan
  unconstrained by current implementation details.
---

# /expert-reset

## Overview

Review the project as if starting it again today, while preserving the current
desired user experience as the product target. The goal is not to make small
local improvements. The goal is to describe the architecture, component model,
data boundaries, tooling, and migration strategy that would be chosen if the
project were being rebuilt with everything now known about the product.

Default to research and feedback only. Do not edit code, refactor files, close
issues, change generated assets, or start implementation unless the user
explicitly asks for that after reviewing the recommendations.

Be bold. Nothing is out of scope for recommendation: architecture, component
structure, build system, data formats, code generation, testing, firmware
layout, web UI delivery, repository structure, release process, documentation,
and migration strategy may all be reconsidered. Still keep recommendations
practical, staged, and grounded in evidence from the repository.

The audience is technical but not development-oriented. Explain the tradeoffs
plainly, avoid unexplained jargon, and connect technical structure back to user
experience, upgrade safety, reliability, and maintainability.

## Review Principles

- Treat the current user experience as the target product, not the current code
  structure.
- Separate "ideal rebuild architecture" from "how to migrate there from here."
- Prefer clear ownership boundaries: product experience, shared configuration
  schema, generated artifacts, firmware integration, web UI, validation, docs,
  and release workflows should each have obvious homes.
- Recommend replacing duplicated or parallel systems with one source of truth
  where that would reduce bugs, improve consistency, or make releases safer.
- Be explicit when a recommendation would be disruptive. Do not dilute it; pair
  it with a staged migration path.
- Do not preserve current implementation choices just because they already
  exist. Preserve them only when they are still the best fit.
- Call out what should not be rebuilt if the current approach is already sound.
- Focus on research quality over volume. Every major recommendation should be
  backed by files, patterns, or observed workflows.

## Workflow

### 1. Establish Current State

Inspect the branch, local changes, project layout, and recent structure.

```bash
git status --short --branch
find . -maxdepth 3 -type f | sort
```

If there are local changes, work with them. Do not revert or overwrite user
work. Since this is a research-only review, avoid changing files unless the user
later asks for implementation.

### 2. Reconstruct the Target Product

Identify the current user experience and the product promises the architecture
must support:

- Main user workflows and configuration flows
- Device and firmware targets
- Web UI surfaces and generated web assets
- Saved settings, schemas, and compatibility expectations
- Build, validation, documentation, and release flows
- Areas where users expect upgrades to preserve behavior

Prefer evidence from source files, generated files, documentation, scripts,
tests, and release workflows over assumptions.

### 3. Find the Natural Architecture

Ask what structure would make the product easiest to understand, extend, test,
and release if starting again:

- What should be the source of truth for cards, controls, icons, screens,
  device definitions, and generated output?
- Which parts should be generated, and which parts should be handwritten?
- Where should validation live so mistakes are caught before firmware or web
  output is built?
- What reusable components or modules should exist across the web UI and
  firmware-facing configuration?
- What boundaries would reduce accidental coupling between device definitions,
  web behavior, generated assets, and documentation?
- What test strategy would protect the user experience and upgrade path?
- What release process would make it obvious that generated output, firmware
  configs, docs, and published assets are in sync?

### 4. Compare Current State to the Reset Design

For each major area, explain:

- Current approach: how it appears to work today
- Reset recommendation: what should be different if rebuilding
- Why: the user, reliability, maintenance, or release benefit
- Evidence: files, duplicated patterns, fragile boundaries, or missing checks
- Migration path: how to move from today without breaking existing users
- Risks: what could go wrong and how to control it

### 5. Be Deliberately Bold

Include at least one section for high-conviction structural changes that may be
too large for a normal refactor review. Examples could include:

- A unified product schema with generators for web, firmware, docs, and tests
- A component library or design-system layer for repeated UI patterns
- A clearer split between authored source, generated artifacts, and published
  webserver output
- A fixture-based compatibility test suite for old and new configurations
- A staged replacement of parallel device or web output paths
- A release gate that proves generated files are current before publishing

Only include examples that are supported by repository evidence.

### 6. Keep It Research-Only

Do not implement changes during this skill run. If implementation ideas arise,
capture them as recommended phases or first experiments.

Run lightweight checks only when they improve confidence in the research.
Avoid expensive firmware compiles or broad generated-file rewrites unless the
user separately asks for deeper validation.

## Output Format

Start with a short plain-English summary of the reset thesis: the architecture
you would choose if starting again and why.

Then provide:

```text
Target product understanding:
- <what the project appears to be optimizing for from the user's perspective>

Recommended reset architecture:
- <top-level architecture and ownership boundaries>

Bold recommendations:
1. <Recommendation title>
   Strength: Strong / Moderate / Exploratory
   What changes: <the architectural or structural change>
   Why it matters: <plain-English benefit>
   Evidence from the repo: <files, patterns, or examples reviewed>
   User experience impact: <should preserve / improves consistency / needs careful migration>
   Migration path: <staged path from current repo to target architecture>
   Risk controls: <tests, compatibility layers, rollout phases, rollback points>
   Effort: Small / Medium / Large
   First experiment: <smallest useful research or implementation step>

What I would keep:
- <current choices that are still good and should not be rebuilt unnecessarily>

Suggested roadmap:
- Phase 1: <research, tests, or low-risk foundations>
- Phase 2: <structural migration>
- Phase 3: <cleanup, enforcement, or release gates>

Not recommended:
- <tempting rebuild ideas that are not worth it, with why>

Checks or research performed:
- <commands, files inspected, or checks skipped with reason>
```

If there is not enough evidence to make a confident recommendation, say what
needs to be inspected next instead of inventing certainty.
