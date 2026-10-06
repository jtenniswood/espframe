## What changes when merged

- Adds Settings → System reset controls for resetting customization or performing a factory reset.
- Matches the supplied Factory Reset layout with a backup callout, side-by-side partial/complete reset panels, and custom confirmation/warning dialogs; the panel starts collapsed.
- Records reset intent in NVS before acknowledging it, resumes cleanup after an interrupted reboot, verifies cleanup, and preserves Wi-Fi plus the Home Assistant API encryption key for customization resets.
- Adds same-origin protection for reset requests and a reset epoch so stale browser sessions cannot restore old settings.
- Factory reset clears saved Wi-Fi credentials and the Home Assistant API encryption key. Wi-Fi credentials compiled into firmware can still reconnect.
- Rejects resets while settings updates or either firmware updater is installing, and releases the reset guard after failed OTA installs.
- Allows same-origin reset requests behind HTTPS reverse proxies.
- Restores backups with clear success/failure feedback while keeping the existing API key when the backup omits it.
- Sends large restores in size-limited atomic batches and retries smaller batches after a device rejects a field, so one invalid setting does not fail the entire backup.

## Automated checks

- [x] `npm run check:fast` passed.
- [x] `npm run check:pr` passed all checks through firmware logic tests; `docs:build` is blocked by the dependency mismatch below.
- [ ] CI checks pass. PR Validation runs #565, #567, and #568 failed during dependency installation before project checks; see the lockfile issue below.

The final `docs:build` step fails because the dependency tree cannot resolve an internal `@material/web` import from `esp-web-tools`. The repository lockfile is missing the `@material/web@2.5.0` and `@lit/context@1.1.6` packages declared by the current dependency overrides; PR Validation runs #565, #567, and #568 also stopped at `npm ci` before project checks.

## Device testing

- [ ] Not needed for this change
- [x] A previous PR Validation firmware was flashed to the Guition V3 at `192.168.10.168`.
- [ ] PR Validation artifact flashed to device
- [ ] Needs device testing before merge

Device testing status: The web restore change has not been flashed. A diagnostic configuration request was rejected with HTTP 400; the device was unreachable during a subsequent read-only check. Confirm device connectivity before hardware validation. Reset behavior, interrupted cleanup recovery, OTA failure recovery, reset blocking during C6 installation, and backup restore still need hardware testing.

PR Validation workflow run/artifact: [run #565](https://github.com/jtenniswood/espframe/actions/runs/37452493792); dependency installation failed, so no firmware artifact was produced.

Firmware artifact (`firmware-test-<device>`): None. Local V1, V2, and V3 factory builds compiled successfully with ESPHome 2026.9.1.

Device tested: Guition V3 at `192.168.10.168` with an earlier PR firmware; this review follow-up was not flashed.

Result/notes: Factory reset, interrupted cleanup recovery, OTA failure recovery, and reset blocking during C6 installs still need hardware validation.

The previous firmware flashed successfully, but factory reset and interrupted-cleanup recovery have not been exercised on physical hardware.

## Notes for reviewers

- ESPHome 2026.9.1 factory firmware compiled successfully for V1, V2, and V3.
- `npm run test:web-smoke` passed, including large backup batches, per-setting failure isolation, and reset-card defaults.
- Reset cleanup intentionally retains the `espframe_rs` bookkeeping namespace so it can resume after power loss.
- Hardware validation should verify customization preserves Wi-Fi and HA API pairing, factory reset clears saved credentials, reset recovers after power loss during cleanup, and reset is blocked while firmware installs are active.
