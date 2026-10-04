## What changes when merged

- Allow the Guition ESP32-P4 firmware to compile with ESPHome 2026.9.1, including the frame identity version guard and decoder IDF component requirements reported in [issue #233](https://github.com/jtenniswood/espframe/issues/233).
- Preserve runtime timezone selection with ESPHome 2026.9's parsed timezone API while retaining compatibility with ESPHome 2026.8.2.

## Automated checks

- [x] `npm run check:pr` passed on the conflict-resolution branch.
- [x] ESPHome 2026.9.1 factory builds passed for Guition ESP32-P4 V1 and V2 on the conflict-resolution branch.
- [x] ESPHome 2026.8.2 factory build passed for V1 on the PR branch before conflict resolution.
- [ ] GitHub Actions CI for the conflict resolution: pending.

## Device testing

- [ ] Not needed for this change
- [ ] PR Validation artifact flashed to device
- [ ] Needs device testing before merge

PR Validation workflow run/artifact: No artifact recorded.

Firmware artifact (`firmware-test-<device>`): Not recorded.

Device tested: Guition ESP32-P4 V1 test display, using the branch development firmware.

Result/notes: OTA succeeded. The device returned to the network, responded to three pings, and its web server returned HTTP 200. ESPHome API logs could not be read because the device uses a Home Assistant-provisioned encryption key that is not present in the local development configuration.

Device-testing status: Hardware behavior still needs confirmation after the conflict resolution.

## Notes for reviewers

- Review device logs and visually confirm the display after providing the ESPHome API encryption key to the local log client.
- Closes #233.
