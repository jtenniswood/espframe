# Contributing translations

Help translate the built-in text shown on the physical EspFrame display: setup
instructions, loading and error messages, photo-source labels, month names, and
relative photo ages. The web interface stays in English. Keep Immich-provided
photo content, entity names, configuration values, and logs unchanged.

The catalogues currently cover English (`en`, the default), German (`de`),
French (`fr`), Spanish (`es`), Italian (`it`), Dutch (`nl`), and Portuguese
(`pt`, Portugal). Select a language under **Device → Display → Device Language**
or Home Assistant's **Device: Language** select.

## Improve an existing translation

Find your `strings.<code>.txt` file in
[`product/translations`](https://github.com/jtenniswood/espframe/tree/main/product/translations).
Compare it with
[`strings.en.txt`](https://github.com/jtenniswood/espframe/blob/main/product/translations/strings.en.txt)
to understand each message, then edit only the translated values after `=`.
Prefer natural, concise wording that fits the display. Translate whole sentences
so the word order works in your language; keep the meaning of setup instructions
and errors intact.

## Add a language

Start from the repository root. For example, to create a Swedish catalogue:

```sh
cp product/translations/strings.en.txt product/translations/strings.sv.txt
```

1. Translate every value in the new UTF-8 file. Keep every English key, including
   full and short month names and singular/plural age messages.
2. Add the language code to the `device_language` options in
   [`product/contract/settings.json`](https://github.com/jtenniswood/espframe/blob/main/product/contract/settings.json).
   Preserve existing codes and the `en` default. Codes use two or three lowercase
   letters, with optional lowercase region suffixes such as `pt-br`.
3. Add its English display name to `makeLanguageCard()` in
   [`docs/webserver/src/settings_screen_cards.ts`](https://github.com/jtenniswood/espframe/blob/main/docs/webserver/src/settings_screen_cards.ts).
   The language chooser is part of the English web interface.
4. Update the supported-language list in this guide, the setting's
   `docs_description`, and `docs/screen-settings.md`.
5. Add representative date and age cases in `tests/espframe_helper_tests.cpp`
   and extend the language-option expectations in `tests/web_smoke_tests.js`.
   The existing catalogue and live-switching tests also exercise the new language.
6. Regenerate, run the checks below, and compile the affected firmware profiles.
   Commit the source changes and regenerated files together.

## Catalogue format

Each entry occupies one physical line, using `key=value`. Keys are lowercase
snake_case and must match the English catalogue exactly. Do not add spaces before
the key or around `=`. Blank lines and lines beginning with `#` are ignored.
An empty translated value falls back to English; the English value must be
nonempty. Mention any untranslated entries in your pull request.

```text
# French examples
wifi_setup=Configuration WiFi
days_ago=il y a {count} jours
check_api_key=Vérifiez votre clé API Immich\ndans les paramètres espframe.
```

| Written in the file | Displayed as |
| --- | --- |
| `\n` | A line break |
| `\\` | A literal backslash |
| `\=` | A literal equals sign |

Preserve the exact placeholders from English: `{count}`, `{address}`, and
`{hotspot}`. You may move them to suit the sentence, but do not rename, omit, or
duplicate them. `{hotspot}` supplies the network name and a trailing line break
when a name is available; `{address}` supplies the captive-portal address.

The generator rejects ambiguous text because live language changes identify an
already visible status by its wording. Shared wording must refer to equivalent
entries in every language. In particular, English uses `May` for both
`month_05` and `month_short_05`, so those two values must also match in each
translation. If natural wording cannot meet the ambiguity check, describe the
collision in your pull request rather than changing unrelated keys.

## Fonts, dates, and plural rules

The setup and menu fonts use the `latin_extended_glyphs` set in
[`assets/fonts.yaml`](https://github.com/jtenniswood/espframe/blob/main/devices/guition-esp32-p4-jc8012p4a1/assets/fonts.yaml).
`npm run test:translations` checks that every catalogue character has font
coverage. A language needing more glyphs, right-to-left layout, or script shaping
requires corresponding firmware changes and display testing.

Relative ages currently choose a singular message for a count of one and a plural
message for larger counts. Languages requiring other plural forms need additional
firmware support. Photo dates keep the chosen existing date layout and translate
its month names; adding a catalogue does not change date order or punctuation.

## Generate and validate

Run these commands from the repository root with Node.js/npm, Python 3, and a
C++17 compiler available:

```sh
npm ci
npm run generate
npm run test:translations
npm run test:helpers
npm run check:pr
```

The checks cover catalogue keys and placeholders, font coverage, live language
switching, generated files, web settings, and backup compatibility. The full
check gate also runs browser tests and builds the docs. Firmware build commands
are in the
[repository development guide](https://github.com/jtenniswood/espframe#development).

Edit the catalogues and declared sources, then regenerate. Do not hand-edit
`components/espframe/i18n_generated.h`, generated setting fields in
`common/addon/translations.yaml`, or the bundled `docs/public/webserver/app.js`.

## Check on a display and submit

Select the language on a display running your build. Review setup instructions,
source labels, loading states, and errors for missing characters, wrapping, and
clipping. Check full and short months, both Date Taken styles, and singular/plural
relative ages. Switch language with a photo and an error visible, confirm that
the current text updates, then check selection persistence after reboot and
backup export/restore.

Open a pull request describing the language and regional variant, wording fixes,
checks actually run, and the display revision tested. Include photos of any
layout problems. If you cannot run checks or test on hardware, say which checks
remain; fluent-speaker review of the wording is welcome.

## Add new device text

For a new static message, add a stable key to `strings.en.txt` and every other
catalogue. Use complete sentences and named placeholders rather than joining
translated fragments. Render it with `espframe_i18n_key("your_key")`, and add
fixed labels to the refresh callback in `common/addon/translations.yaml` so they
update when the language changes. Then regenerate and test as above.
