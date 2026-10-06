## What changes when merged

- Backups no longer export the Immich API key. Restore keeps the destination's current key unchanged and ignores keys in older backup files; when no key is configured, the UI directs the user to the highlighted API Key field.
- Backup restore reports actual failed settings and retries a rejected batch field by field.

## Automated checks

- [ ] `npm run check:pr` passed; it reached `docs:build` and stopped because `test:docs-installer` expected P4 revision 301 behavior from the installed esptool-js bundle.
- [ ] CI checks passed after this update

Passed: `npm run check:fast`, `npm run webserver:typecheck`, all tests through `npm run test:firmware-logic`, and the browser smoke suite. The focused restore scenarios verified that new exports omit the API key, legacy imports ignore it, and an unconfigured destination opens the Immich Connection card with only the API Key field highlighted.

## Device testing

- [ ] Not needed for this change
- [ ] PR Validation artifact flashed to device
- [ ] Needs device testing before merge

PR Validation workflow run/artifact: Pending for this update. Earlier CI stopped during dependency installation because `package.json` and `package-lock.json` are out of sync (`@material/web@2.5.0` and `@lit/context@1.1.6` are missing from the lock file).

Firmware artifact (`firmware-test-<device>`): V1, V2, and V3 compiled locally with ESPHome 2026.9.1.

Device tested: Not flashed for this update.

Result/notes: Exported backups omit the key; restore ignores keys in older backup files, preserves the destination key, and guides users to configure a key when the destination has none.

## Notes for reviewers

- API-key reads remain masked. Exports omit the key, and imports ignore the key in legacy files.
- Browser runtime does not retain the key in persistent settings state.
- The import fallback retries validation failures per field, and the destination key remains unchanged after restore.
