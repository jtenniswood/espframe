# Testing Espframe

Espframe has several types of checks. They are split so day-to-day changes can be tested quickly, while release checks still cover the slower firmware and publishing safeguards.

## Recommended Checks

Before opening a pull request, run:

```sh
npm run check:pr
```

This is the normal confidence check for feature branches. It verifies generated files, product metadata, backup compatibility, web UI behavior, firmware helper logic, timezone data, and the documentation build. GitHub runs this automatically on every pull request as the `Validate PR Gate` check.

Before publishing a firmware release, run:

```sh
npm run check:release-ready-with-compile
```

This runs the full release validation, checks the working tree is clean, and compiles both factory and OTA firmware with ESPHome Docker. If you only need the non-compile release gate while iterating, run `npm run check:release`.

For the fastest local check while editing metadata, generated assets, or compatibility fixtures, run:

```sh
npm run check:fast
```

This skips browser and documentation build work, so it is useful while iterating.

## Test Groups

### Fast Project Checks

```sh
npm run check:fast
```

This group checks generated files, product metadata, backup configuration, compatibility fixtures, and generated firmware fields. These checks catch mistakes where the user-visible settings contract, generated web files, firmware fields, or docs metadata drift apart.

### Web UI Checks

```sh
npm run test:web
```

This group checks the generated web app bundle, compatibility helpers, focused smoke-test command options, and browser smoke coverage. The browser smoke test opens the web UI in Chrome or Chromium and exercises the main setup, settings, firmware update, and backup import flows.

When you are working on one browser flow, run a single smoke scenario while iterating:

```sh
npm run test:web-smoke -- --scenario wizard-connection-save
```

Use `npm run test:web-smoke -- --list` to see the available scenario names. Run the full `npm run test:web-smoke` suite before committing web UI behavior changes.

### Firmware Logic Checks

```sh
npm run test:firmware-logic
```

This group compiles and runs host-side C++ tests for firmware helper logic, then checks timezone data. It is much faster than a full ESPHome compile and is the right place to cover slideshow decisions, Immich request building, date handling, duration parsing, and other logic that can be tested without a device.

### Full Firmware Compile

Pull requests run the normal validation gate automatically. Full ESPHome firmware builds are slower, so they are available from the **PR Validation** workflow's manual run button. Run that workflow against a feature branch when you need firmware files to test on a device before merging.

The manual workflow builds both factory and OTA firmware and uploads a `firmware-test-<device>` artifact containing:

- `<device>.factory.bin` for USB/browser flashing
- `<device>.ota.bin` for OTA testing
- `<device>.version.txt` with the branch build version and source commit

To run the same factory compile locally with Docker:

```sh
docker run --rm -v "${PWD}:/config" ghcr.io/esphome/esphome:2026.6.4 compile /config/builds/guition-esp32-p4-jc8012p4a1.factory.yaml
```

Use a full compile before firmware releases, after changing ESPHome YAML, after changing C++ code that is not covered by the host-side helper tests, and whenever you want a branch firmware build to flash to a test display.

## When To Add Tests

Add or update tests with the feature change, especially when changing:

- device settings, backup import/export, or compatibility behavior
- Immich request building, photo source filtering, portrait pairing, or slideshow flow
- generated firmware fields, product metadata, or public docs tables
- the web settings UI, startup wizard, firmware update controls, or backup restore flow
- release files, firmware manifests, or changelog behavior

Prefer fixture-based tests for saved settings and compatibility changes. Add host-side C++ helper tests for firmware decisions that do not need hardware. Add browser smoke coverage when the user-facing web flow changes.

## Manual Device Checks

Automated checks do not prove everything that happens on the physical display. Do a short device check when changing display behavior, touch behavior, networking setup, image rendering, OTA updates, or anything that depends on ESPHome runtime behavior.

A useful manual pass is:

- run the manual **PR Validation** workflow for the branch and flash the downloaded firmware artifact to a test display
- confirm WiFi setup and Immich setup still work
- confirm the slideshow starts and advances photos
- check touch wake, sleep, and next-photo gestures
- open the device web UI and change a setting
- export a backup, restore it, and confirm important settings survive
- check firmware update status if release/update behavior changed

Record any manual device testing in the pull request so reviewers know what was verified outside automation.
