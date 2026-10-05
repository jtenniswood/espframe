## What changes when merged

- Renames the screen selection page and its navigation/home-page links to “Where to Buy.” The existing `/screens` URL and display identification guidance stay in place.

## Automated checks

- [x] `npm run check:pr` passed
- [x] CI checks are expected to pass

## Device testing

- [x] Not needed for this change
- [ ] PR Validation artifact flashed to device
- [ ] Needs device testing before merge

PR Validation workflow run/artifact: Not applicable; documentation-only change.

Firmware artifact (`firmware-test-<device>`):

Device tested: Not applicable.

Result/notes: `npm run check:pr` passed, including the VitePress build and docs site checks.

## Notes for reviewers

- The page remains at `/screens` so existing links and bookmarks continue to work.
