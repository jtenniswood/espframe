## What changes when merged

- Add a distinct ESP32-P4 v3.x production-silicon firmware profile for the Guition JC8012P4A1 10-inch display.
- Reuse the V2 display, touch, WiFi, and layout configuration while setting production silicon and using the V3 MIPI DSI PHY default fix.
- Give V3 its own build outputs, device identity, stable/beta manifest paths, and OTA manifest URL so updates stay on V3 artifacts.
- Preserve the existing V1 and V2 profiles and update flows. Add manual V3 package setup while keeping the existing ESPHome 2026.8.2 pin.

## Automated checks

- [x] `npm run check:pr` passed
- [ ] CI checks are expected to pass

## Device testing

- [ ] Not needed for this change
- [ ] PR Validation artifact flashed to device
- [ ] Needs device testing before merge

PR Validation workflow run/artifact: Not run

Firmware artifact (`firmware-test-<device>`): V1 factory and V3 factory/OTA compiles passed; V2 compile pending. PR validation artifact not run

Device tested: None

Result/notes: No physical V3 USB installation or OTA update has been tested. Compile validation is tracked separately from device behavior.

## Notes for reviewers

- V3 stable and beta manifests will be published with a firmware release; docs/release workflows allow them to be absent before the first V3 release.
- Follow-up PR groups V1/V2/V3 in the web installer and adds model/revision setup guides.
