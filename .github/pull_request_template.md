## What changes when merged

- Backups now export and restore the Immich API key; older backups without a key preserve the destination key. Backup files contain the key in plain text.
- Backup restore reports actual failed settings and retries a rejected batch field by field.

## Automated checks

- [ ] `npm run check:pr` passed; the complete check did not pass locally because the docs installer regression test expects P4 revision 301 behavior that is absent from the installed dependency bundle.
- [ ] CI checks are expected to pass

Passed locally: `npm run check:fast`, `npm run webserver:typecheck`, `npm run test:web-saves`, `npm run test:web-api-client`, `npm run test:web-types`, `npm run test:web-compat`, `npm run test:web-modules`, `npm run test:web-smoke-cli`, `npm run test:web-smoke`, `npm run test:package-contract`, `npm run test:product-contract-common`, `npm run test:release-ready`, `npm run test:workflow-contract`, `npm run test:budgets`, `npm run test:ownership`, `npm run test:timezones`, `npm run test:memory-diagnostics`, `npm run test:photo-buffers`, and `npm run test:automation-controller`. `npm run test:parsers` passed when rerun with network access.

## Device testing

- [ ] Not needed for this change
- [ ] PR Validation artifact flashed to device
- [ ] Needs device testing before merge

PR Validation workflow run/artifact: CI stopped during dependency installation because `package.json` and `package-lock.json` are out of sync (`@material/web@2.5.0` and `@lit/context@1.1.6` are missing from the lock file).

Firmware artifact (`firmware-test-<device>`): Locally compiled with ESPHome 2026.8.2 from commit `ddb6276`.

Device tested: Guition ESP32-P4 JC8012P4A1 V3 at `192.168.10.168`.

Result/notes: OTA upload succeeded. After reboot, HTTP returned 200; regular key reads stayed masked, while the backup opt-in returned the configured key with `Cache-Control: no-store`.

## Notes for reviewers

- API-key export uses a dedicated opt-in response with `Cache-Control: no-store`; normal configuration reads remain masked.
- Browser runtime does not retain the key in persistent settings state.
- Exports contain the Immich API key in plain text; documentation warns users to store backups securely.
- The import fallback retries validation failures per field, and older backups without a key leave the destination key unchanged.
