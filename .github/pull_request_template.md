## What changes when merged

- Backups now export and restore the Immich API key; older backups without a key preserve the destination key. Backup files contain the key in plain text.
- Backup restore reports actual failed settings and retries a rejected batch field by field.

## Automated checks

- [ ] `npm run check:pr` passed (blocked locally by Chrome SIGTRAP in browser smoke; offline DNS also blocked the Immich parser fixture download)
- [ ] CI checks are expected to pass

## Device testing

- [ ] Not needed for this change
- [ ] PR Validation artifact flashed to device
- [x] Needs device testing before merge

PR Validation workflow run/artifact:

Firmware artifact (`firmware-test-<device>`):

Device tested: Pending this revision; V3 was tested on an earlier PR commit.

Result/notes: V3 compile and OTA verification are pending.

## Notes for reviewers

- API-key export uses a dedicated opt-in response with `Cache-Control: no-store`; normal configuration reads remain masked.
- Browser runtime does not retain the key in persistent settings state.
