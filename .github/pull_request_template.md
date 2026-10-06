## What changes when merged

- Adds Settings → System reset controls for resetting customization or performing a factory reset.
- Matches the supplied Factory Reset layout with a backup callout, side-by-side partial/complete reset panels, and custom confirmation/warning dialogs.
- Records reset intent in NVS before acknowledging it, resumes cleanup after an interrupted reboot, verifies cleanup, and preserves Wi-Fi plus the Home Assistant API encryption key for customization resets.
- Adds same-origin protection for reset requests and a reset epoch so stale browser sessions cannot restore old settings.
- Factory reset clears saved Wi-Fi credentials and the Home Assistant API encryption key. Wi-Fi credentials compiled into firmware can still reconnect.

## Automated checks

- [ ] `npm run check:pr` passed
- [ ] CI checks are expected to pass

`npm run check:pr` passed every check through the firmware logic tests. `docs:build` is blocked by a pre-existing dependency mismatch on `main`: `package.json` overrides `@material/web` to 2.5.0, while `package-lock.json` does not contain that package version. A clean `npm ci` reports missing `@material/web@2.5.0` and `@lit/context@1.1.6`; installing from `package.json` then makes VitePress fail to resolve an import used by `esp-web-tools`.

## Device testing

- [ ] Not needed for this change
- [ ] PR Validation artifact flashed to device
- [ ] Needs device testing before merge

Device testing status: PR firmware flashed to the Guition V3 display at 192.168.10.168. Reset behavior and cleanup recovery have not been exercised on the device.

PR Validation workflow run/artifact:

Firmware artifact (`firmware-test-<device>`):

Device tested:

Result/notes:

Factory reset and interrupted-cleanup recovery have not been exercised on physical hardware.

## Notes for reviewers

- ESPHome 2026.9.1 factory firmware compiled successfully for V1, V2, and V3.
- Reset cleanup intentionally retains the `espframe_rs` bookkeeping namespace so it can resume after power loss.
- Hardware validation should verify customization preserves Wi-Fi and HA API pairing, factory reset clears saved credentials, and reset recovers after power loss during cleanup.
