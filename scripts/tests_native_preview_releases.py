"""Check version selection and the board metadata in preview binaries."""

import struct
import unittest

from native_preview_releases import image_metadata, published_versions


class NativePreviewReleasesTest(unittest.TestCase):
    def test_published_native_versions(self):
        def release(tag, draft=False, prerelease=True):
            return dict(tagName=tag, isDraft=draft, isPrerelease=prerelease)

        self.assertEqual(published_versions([
            release("v99.0.0"),
            release("native-v0.1.0-preview.2"),
            release("native-v0.1.0-preview.10"),
            release("native-v0.1.0-preview.11", draft=True),
            release("native-v0.1.0-preview.12", prerelease=False),
            release("native-v0.1.0-preview.01"),
        ]), ["v0.1.0-preview.10", "v0.1.0-preview.2"])

    def test_image_board_and_embedded_version(self):
        image = bytearray(288)
        image[0] = 0xE9
        image[23] = 1
        struct.pack_into("<H", image, 12, 0x12)
        struct.pack_into("<I", image, 32, 0xABCD5432)
        version = "v0.1.0-preview.4"
        image[48:48 + len(version)] = version.encode("ascii")
        image[80:98] = b"espframe_native_v1"
        image[176:208] = bytes(range(32))
        result = image_metadata(image, "v1", version)
        self.assertEqual(result["version"], version)
        self.assertEqual(result["size"], 288)
        self.assertEqual(len(result["sha256"]), 64)
        self.assertEqual(result["elf_sha256"], bytes(range(32)).hex())
        with self.assertRaises(ValueError):
            image_metadata(image, "v2", version)
        with self.assertRaises(ValueError):
            image_metadata(image, "v1", "v0.1.0-preview.5")


if __name__ == "__main__":
    unittest.main()
