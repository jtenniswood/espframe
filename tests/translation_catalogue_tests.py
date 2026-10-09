#!/usr/bin/env python3
"""Exercise catalogue validation, device coverage, and shipped font coverage."""
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from asset_generation.translations import load_catalogues, load_strings


class TranslationCatalogueTests(unittest.TestCase):
    def catalogues(self, english, german):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name)
        (path / "strings.en.txt").write_text(english)
        (path / "strings.de.txt").write_text(german)
        return load_catalogues(path)

    def test_unicode_escapes_and_equals(self):
        catalogues = self.catalogues("text=Line\\npath\\\\file\\=value=tail\n", "text=März\\nPfad\\\\Datei\\=Wert=Ende\n")
        self.assertEqual(catalogues["de"]["text"], "März\nPfad\\Datei=Wert=Ende")

    def test_empty_translation_falls_back(self):
        self.assertEqual(self.catalogues("text=English\n", "text=\n")["de"]["text"], "English")

    def test_duplicate_and_invalid_keys(self):
        for source in ("text=One\ntext=Two\n", "Bad key=One\n", "missing separator\n", "text=bad\\q\n"):
            with self.subTest(source=source), self.assertRaises(ValueError):
                self.catalogues(source, "text=Deutsch\n")

    def test_keys_must_match(self):
        for german in ("", "extra=Deutsch\n", "text=Deutsch\nextra=Extra\n"):
            with self.subTest(german=german), self.assertRaises(ValueError):
                self.catalogues("text=English\n", german)

    def test_placeholders_may_move_but_must_match(self):
        self.catalogues("text={count} years ago\n", "text=vor {count} Jahren\n")
        for german in ("text=vor Jahren\n", "text=vor {number} Jahren\n", "text={count} {count}\n"):
            with self.subTest(german=german), self.assertRaises(ValueError):
                self.catalogues("text={count} years ago\n", german)

    def test_ambiguous_refresh_text_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Ambiguous"):
            self.catalogues("one=One\ntwo=Two\n", "one=Gleich\ntwo=Gleich\n")
        # Shared full/short month names are fine if every translation agrees.
        self.catalogues("one=May\ntwo=May\n", "one=Mai\ntwo=Mai\n")

    def test_every_firmware_key_is_in_catalogue(self):
        english = load_catalogues()["en"]
        paths = list((ROOT / "common").rglob("*.yaml")) + list((ROOT / "devices").rglob("*.yaml"))
        paths += [p for p in (ROOT / "components/espframe").glob("*.h") if p.name != "i18n_generated.h"]
        for path in paths:
            for key in re.findall(r'espframe_i18n_key\("([^"]+)"\)', path.read_text()):
                self.assertIn(key, english, str(path))

    def test_every_catalogue_is_declared_as_a_generated_source(self):
        project = json.loads((ROOT / "product/contract/project.json").read_text())
        for path in (ROOT / "product/translations").glob("strings.*.txt"):
            self.assertIn(path.relative_to(ROOT).as_posix(), project["generated_asset_sources"])

    def test_shipped_strings_fit_existing_ui_fonts(self):
        fonts = (ROOT / "devices/guition-esp32-p4-jc8012p4a1/assets/fonts.yaml").read_text()
        latin = re.search(r'  latin_extended_glyphs: >-\n((?:    .*\n)+)', fonts)[1]
        glyphs = set(latin) | {"\n"}
        for code, strings in load_catalogues().items():
            for key, value in strings.items():
                self.assertFalse(set(value) - glyphs, f"{code}:{key} missing font glyphs")

    def test_month_names_start_with_capitals(self):
        for code, strings in load_catalogues().items():
            for key, value in strings.items():
                if re.fullmatch(r"month(?:_short)?_\d{2}", key):
                    self.assertTrue(value[0].isupper(), f"{code}:{key} must start with a capital")

    def test_english_connection_messages_preserve_contract(self):
        english = load_catalogues()["en"]
        project = json.loads((ROOT / "product/contract/project.json").read_text())
        self.assertEqual(english["invalid_api_key"], project["connection_invalid_api_key_title"])
        self.assertEqual(english["connection_failed"], project["connection_unavailable_title"])


if __name__ == "__main__":
    unittest.main()
