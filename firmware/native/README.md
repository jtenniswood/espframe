# Native Immich player prototype

This standalone ESP-IDF app exercises the photo gesture on the Guition
JC8012P4A1. A background task keeps two slides behind and up to three ahead
in memory as panel-ready pixels. Drag either way, release a short drag,
flick, or press again to take over a moving slide.

With no settings provisioned, it starts on slide three in a sequence of local
fixtures and works offline. It uses the matching panel initialization sequence
from the maintained Espframe device profile and the GSL3680 firmware and
point identification sources already in the repository. See
[the touchscreen component's attribution](../../components/gsl3680/LICENSE.md).

## Build

Use ESP-IDF 6.1.1. From the repository root:

```sh
idf.py -C firmware/native -B firmware/native/build/v3 -D SWIPE_BOARD=v3 build merge-bin
```

Choose v1 for the original panel, v2 for the updated panel on pre-v3 silicon,
or v3 for production v3.x silicon. Match the model used by the regular
Espframe installer and use a different build directory per board. The
merged binary is in the selected build directory. Use USB on a dedicated
test frame after exporting its settings. Have the regular Espframe
factory image for that revision available to restore the device after the
test. The prototype uses USB installation outside the regular OTA and
browser installer flow.

Host regression checks run without the ESP-IDF toolchain:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/native/main \
  firmware/native/tests/swipe_test.cpp -o /tmp/espframe-swipe-test
/tmp/espframe-swipe-test
c++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/native/main \
  firmware/native/tests/slide_cache_test.cpp -o /tmp/espframe-slide-cache-test
/tmp/espframe-slide-cache-test
python3 firmware/native/tests/test_preview_decoder.py
```

## Connect to Immich on a test frame

Provision Wi-Fi and a read-only Immich API key with the `asset.read` and
`asset.view` permissions. Activate ESP-IDF on the host and run the settings
generator.
The base URL accepts a reverse proxy prefix and may end in `/api`.
For HTTPS, use a certificate trusted by the ESP-IDF certificate bundle. The
frame synchronizes its clock with `pool.ntp.org` before its first TLS request.

```sh
python3 firmware/native/tools/provision.py /tmp/espframe-native-settings.bin
python -m esptool --chip esp32p4 --port PORT write-flash 0x410000 /tmp/espframe-native-settings.bin
```

Use that flash offset with this prototype's partition table on the dedicated
test frame. The generated file contains the Wi-Fi password and API key in
plaintext. Keep it private and remove it after provisioning. The player shows a
local scene until the first preview is ready, then starts the slideshow. It
loads additional photos directly from Immich and advances every 15 seconds
when a new slide is ready. It keeps the current photo visible during a network
failure. This build uses a random image feed and retains up to 1,999 positions
per boot, with a fitted single image per position.

## Test on the frame

1. Boot to the middle scene and drag toward each adjacent scene slowly. The
   image boundary should follow your finger with no visible tearing.
2. Drag only a short distance and pause before lifting. The view should
   settle back. Repeat with a quick flick and check that it advances.
3. Start a transition, then grab it again and pull back. Repeat rapid
   forward and backward swipes, including the split scene.
4. Check the first and last scenes. Dragging beyond those boundaries should
   keep the displayed pixels filled.
5. Record high-frame-rate video for touch-to-visible-motion and pacing.
   USB logs report recognition-to-DMA-scanout as `gesture_to_scanout_us`.
   This is an estimate before the new pixels are physically visible.

To probe with real photos, place baseline JPEGs in the ignored
`firmware/native/photos/` directory as `slide-0.jpg` through `slide-4.jpg`.
Prepare the landscape image as 1280 × 800, rotate it 90 degrees clockwise
into 800 × 1280, and bake in the rotation. Compose a portrait pair into the
landscape canvas before rotating. After adding files, run the same command
with `reconfigure` before `build`. Missing or rejected JPEGs use the built-in test scenes.

With Immich configured, repeat the gesture checks while a JPEG or WebP is
downloading and after disconnecting Wi-Fi. Cached neighbors should still
move together and reverse. Reconnect Wi-Fi and confirm the gallery resumes
prefetching while keeping the same photo on screen.
