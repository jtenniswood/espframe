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

Physical-display text lives in `product/translations/strings.en.txt`, following
EspControl's `key=value` translation format. German uses `strings.de.txt`.
Translate only the text after `=`; keep keys and placeholders such as `{count}`,
`{address}`, and `{hotspot}` unchanged. Use `\n` for line breaks, `\\` for a
literal backslash, and `\=` for a literal equals sign. An empty translated value
falls back to English.

For a new language, copy the English catalogue to `strings.<code>.txt`, translate
its values, add the code to the `device_language` options in
`product/contract/settings.json`, and add its display name in
`docs/webserver/src/settings_screen_cards.ts`. Confirm the UI fonts cover its
characters; scripts needing shaping or different plural rules need additional
firmware work. Relative ages currently use singular and plural forms.

Use `espframe_i18n_key("stable_key")` when rendering static device text. Keep
complete sentences in the catalogue so translations can change word order. Add
new fixed labels to the refresh callback in `common/addon/translations.yaml`
so they update when the language changes. Do not translate logs, entity names,
configuration option values, or Immich-provided content.

Run `npm run generate` and `npm run check:pr` after editing. The generator checks
matching keys, placeholders, and unambiguous status text. Edit the catalogue,
not the generated `components/espframe/i18n_generated.h`.
