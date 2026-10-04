## What changes when merged

- Project metadata and local build instructions pin ESPHome to 2026.9.1.
- Firmware uses ESPHome's parsed timezone API and explicitly registers the local JPEG/WebP ESP-IDF components required by 2026.9.1.
- Generated timezone data and the frame identity version guard are updated for 2026.9.1.

## Automated checks

- [x] `npm run check:pr` passed
- [ ] GitHub Actions CI: pending PR creation.
- [x] V1 and V2 factory firmware compiled with ESPHome 2026.9.1.

## Device testing

- [ ] Not needed for this change
- [ ] PR Validation artifact flashed to device
- [ ] Needs device testing before merge

PR Validation workflow run/artifact:

Not run; local factory builds only.

Firmware artifact (`firmware-test-<device>`): None.

Device tested: None.

Result/notes: V1 and V2 factory builds passed. No hardware was flashed.

Device-testing status: Required before merge; no device was flashed.

## Notes for reviewers

- Follow up with device testing for timezone changes and JPEG/WebP decoding.
