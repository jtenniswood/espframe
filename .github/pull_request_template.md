## What changes when merged

- Consolidate versioned configuration and legacy entity writes behind one typed API client.
- Negotiate the generated capabilities contract, retain the legacy adapter, and classify timeout/offline/validation/conflict/server failures.

## Automated checks

- [ ] `npm run check:pr` passed (browser smoke is blocked locally by Chrome SIGTRAP/crashpad permissions)
- [x] CI checks are expected to pass

## Device testing

- [ ] Not needed for this change
- [ ] PR Validation artifact flashed to device
- [ ] Needs device testing before merge

PR Validation workflow run/artifact:

Firmware artifact (`firmware-test-<device>`):

Device tested:

Result/notes:

## Notes for reviewers

- Local PR checks passed through web smoke CLI and documentation build. The full gate reached browser smoke but Chrome exited with `SIGTRAP` after `crashpad ... setsockopt: Operation not permitted`.
- Firmware behavior is unchanged; no device flash was performed.
