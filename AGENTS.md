# Agent contribution workflow

When a task changes tracked project files, deliver the work through a GitHub
pull request unless the user explicitly asks for a different workflow.
Read-only tasks, reviews, investigations, and tasks that do not change tracked
files do not require an empty pull request.

## Before editing

- Inspect the branch and working tree. Never stage, stash, commit, overwrite,
  or revert unrelated user changes.
- If the checkout is dirty with unrelated work or is already serving another
  task, create a separate worktree from the latest `origin/main`.
- Never commit directly to `main`. Create or use a focused non-default branch
  for the task and keep unrelated changes out of it.
- Review `product/source-ownership.json` before changing generated, vendored,
  or externally sourced files. Edit declared sources and run `npm run generate`
  instead of hand-editing generated outputs. Change vendored code only when the
  task requires it, preserving its attribution and revision metadata.

## Implementation requirements

- Keep the change focused. Do not add opportunistic refactors, dependency
  upgrades, formatting sweeps, or unrelated documentation changes.
- Unless the user explicitly approves a breaking change and migration plan,
  preserve ESPHome and Home Assistant entity names, saved preferences,
  configuration defaults and fields, backup compatibility, public web
  endpoints, and existing device support.
- Add or update regression coverage for changed behavior when practical. Use
  compatibility fixtures for settings and backups, host-side C++ tests for
  firmware decisions, and browser/module tests for web behavior.
- Split large work into independently reviewable stages that preserve existing
  behavior between pull requests.

## Validation

- Follow `docs/testing.md` and run `npm run check:pr` before opening or updating
  the pull request.
- For ESPHome YAML changes or C++ changes not covered by host-side tests, run the
  relevant full firmware compile. If compile or hardware validation is not
  available, say so and mark the pull request as needing that validation.
- Never claim a check, compile, device test, or manual test passed unless it was
  actually run. Report exact failures and distinguish pre-existing failures
  from failures caused by the change.

## Delivery and authorization

- Commit and push the completed changes, then open a pull request against the
  repository's default branch. Include its URL in the final response.
- Complete `.github/pull_request_template.md` with the user-visible result,
  checks actually run, device-testing status, known limitations, and relevant
  follow-up work.
- Monitor the initial CI result and fix failures caused by the pull request.
- Leave the pull request open for review. Do not merge pull requests, create
  releases or tags, deploy documentation, flash devices, change repository
  settings, or trigger destructive workflows unless the user explicitly asks.
- If authentication, permissions, missing remotes, failing required checks, or
  other blockers prevent delivery, report the blocker clearly and preserve the
  prepared local work.
