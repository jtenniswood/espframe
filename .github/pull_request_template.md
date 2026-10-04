## What changes when merged

- Removes the Testing documentation page and its Advanced navigation link.
- Removes the deleted route from docs discovery checks and contract validation.
- Keeps the PR validation command in `AGENTS.md` without linking to the removed page.

## Automated checks

- [x] `npm run check:pr` passed
- [ ] CI checks are expected to pass

## Device testing

- [x] Not needed for this change
- [ ] PR Validation artifact flashed to device
- [ ] Needs device testing before merge

PR Validation workflow run/artifact:

Firmware artifact (`firmware-test-<device>`):

Device tested:

Result/notes:

## Notes for reviewers

- No firmware behavior changed. Full `npm run check:pr` passed locally.
