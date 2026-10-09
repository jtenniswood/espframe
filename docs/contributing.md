---
title: Contributing to EspFrame
description: How to propose and test documentation, firmware, and web interface changes for EspFrame.
---

# Contributing to EspFrame

Thanks for wanting to help improve EspFrame. Contributions can improve the firmware, the device web interface, documentation, or the tools that keep generated files in sync.

The [developer setup in the repository README](https://github.com/jtenniswood/espframe#development) covers the local docs server and firmware builds. The [testing guide](https://github.com/jtenniswood/espframe/blob/main/docs/testing.md) lists the project checks, and [source ownership](https://github.com/jtenniswood/espframe/blob/main/docs/source-ownership.md) explains which files are generated and where to make their source changes.

## Before opening a pull request

- Check the [open issues](https://github.com/jtenniswood/espframe/issues) for existing discussions.
- Keep each pull request focused so it is easier to review and test. For a larger feature or a change that affects device behavior, open an issue first to agree on the approach.
- Update the documentation when a change affects how people install or use EspFrame.
- Keep existing device support, saved settings, and Home Assistant entity names compatible unless a breaking change has been discussed first.
- If a file is generated or vendored, follow the source ownership guidance instead of editing generated output directly.

## Pull request testing

Create a focused branch from `main` and review the complete diff before submitting. Install the locked dependencies with `npm ci`, then run the standard pull request checks:

```sh
npm run check:pr
```

For ESPHome YAML changes, or C++ changes that are not covered by host-side checks, compile the affected firmware profile too. A successful check or compile confirms that the project builds; it does not replace testing on a physical display when the change affects device behavior.

The [pull request template](https://github.com/jtenniswood/espframe/blob/main/.github/pull_request_template.md) asks for the user-visible result, checks actually run, firmware and device testing status, and known limitations. Include screenshots for visible web interface changes. For device behavior changes, name the affected panel revision and the physical checks reviewers should perform.

See [USB log collection](/collect-usb-logs) if you need to attach startup diagnostics to an issue or pull request.

## Device translations

The [translation contribution guide](/translations) explains how to improve
existing wording or add a language, preserve placeholders, regenerate the
catalogues, and test the result on a display. Its source lives alongside the
catalogues in
[`product/translations/README.md`](https://github.com/jtenniswood/espframe/blob/main/product/translations/README.md),
and the docs include that same guide so both versions stay in sync.
