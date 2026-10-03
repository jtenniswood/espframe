## What changes when merged

- Allow the Guition ESP32-P4 firmware to compile with ESPHome 2026.9.1, including the frame identity version guard and decoder IDF component requirements reported in [issue #233](https://github.com/jtenniswood/espframe/issues/233).
- Preserve runtime timezone selection with ESPHome 2026.9's parsed timezone API while retaining compatibility with ESPHome 2026.8.2.

## Automated checks

- [x] `npm run check:pr` passed
- [x] CI checks are expected to pass
- ESPHome 2026.9.1 factory builds passed for Guition ESP32-P4 V1 and V2.
- ESPHome 2026.8.2 factory build passed for V1.

## Device testing

- [ ] Not needed for this change
- [ ] PR Validation artifact flashed to device
- [x] Needs device testing before merge

PR Validation workflow run/artifact:

Firmware artifact (`firmware-test-<device>`):

Device tested: Guition ESP32-P4 V1 test display, using the branch development firmware.

Result/notes: OTA succeeded. The device returned to the network, responded to three pings, and its web server returned HTTP 200. ESPHome API logs could not be read because the device uses a Home Assistant-provisioned encryption key that is not present in the local development configuration.

## Notes for reviewers

- Follow-up needed: review device logs and visually confirm the display after providing the ESPHome API encryption key to the local log client.
- Closes #233.
