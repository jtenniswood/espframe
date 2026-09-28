## What changes when merged

- Add a grouped 10-inch JC8012P4A1 revision picker with explicit V1, V2, and V3 choices and revision identification hints.
- Require a revision selection before installation, use each profile's own firmware manifest, and probe unpublished V2/V3 manifests independently.
- Add a model overview and V1, V2, and V3 setup guides; link them from the install and overview pages.
- Explain that V3 is identified by ESP32-P4 v3.x chip information and that it retains the V1-style web installer, on-device OTA, and device-page upload routes.

## Automated checks

- [x] `npm run check:pr` passed
- [ ] CI checks are expected to pass

## Firmware compile checks

- V1 factory, V2 factory, V3 factory, and V3 OTA configurations compiled with ESPHome 2026.8.2.
- Physical V3 USB installation and OTA update have not been tested; compile success does not confirm device behavior.

## Device testing

- [ ] Not needed for this change
- [ ] PR Validation artifact flashed to device
- [ ] Needs device testing before merge

PR Validation workflow run/artifact: Not run

Firmware artifact (`firmware-test-<device>`): PR validation artifact not run

Device tested: None

Result/notes: No physical V3 initial USB installation or OTA update has been tested. Compile validation is tracked separately from device behavior.

## Notes for reviewers

- V2/V3 installer choices remain hidden until their release manifests are published.
- The V3 setup guide documents chip-based identification and the inherited update routes; USB and OTA behavior still needs validation on production-silicon hardware.
