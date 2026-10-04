## What changes when merged

- Add a grouped 10-inch JC8012P4A1 revision picker with explicit V1, V2, and V3 choices and revision identification hints.
- Require a revision selection before installation, use each profile's own firmware manifest, and probe unpublished V2/V3 manifests independently.
- Add a model overview and V1, V2, and V3 setup guides; link them from the install and overview pages.
- Explain that V3 is identified by ESP32-P4 v3.x chip information and that it retains the V1-style web installer, on-device OTA, and device-page upload routes.
- Pin project metadata and local build instructions to ESPHome 2026.9.1.
- Use ESPHome's parsed timezone API and register the local JPEG/WebP ESP-IDF components required by 2026.9.1.
- Update generated timezone data and the frame identity version guard for 2026.9.1.

## Automated checks

- [x] `npm run check:pr` passed after conflict resolution
- [ ] GitHub Actions CI after conflict resolution

## Firmware compile checks

- V1 factory, V2 factory, V3 factory, and V3 OTA configurations compiled with ESPHome 2026.9.1.

## Device testing

- [ ] Not needed for this change
- [ ] PR Validation artifact flashed to device
- [ ] Needs device testing before merge

PR Validation workflow run/artifact: Pending GitHub checks; no artifact produced by local validation

Firmware artifact (`firmware-test-<device>`): None

Device tested: None

Result/notes: No physical V3 initial USB installation or OTA update has been tested. Compile validation does not confirm device behavior.

Device-testing status: Required before describing V3 installation and OTA behavior as verified.

## Notes for reviewers

- V2/V3 installer choices remain hidden until their release manifests are published.
- The V3 setup guide documents chip-based identification and the inherited update routes; USB and OTA behavior still needs validation on production-silicon hardware.
- Follow up with device testing for timezone changes and JPEG/WebP decoding.
