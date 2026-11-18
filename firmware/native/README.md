# Native Immich player prototype

This standalone ESP-IDF app exercises the photo gesture on the Guition
JC8012P4A1. A background task keeps up to three slides in the direction of
travel and two in the other direction in memory as panel-ready pixels.
Drag either way, release a short drag, flick, or press again to take over a
moving slide.

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
test. You can use the prototype browser installer or the ESP-IDF USB tools.

### Install and connect in a browser

Download and extract the `native-browser-installer` artifact from the Native
Swipe Prototype workflow. From the extracted directory run:

```sh
python3 -m http.server 8080 --bind 127.0.0.1
```

Open <http://localhost:8080> in desktop Chrome or Edge. Select the frame
revision and install the firmware. Close the installer dialog when it
finishes, then enter your Wi-Fi and Immich settings and select the frame's
USB port. The browser sends settings directly over USB and the frame
restarts when it saves them.

To build the same installer locally, run `npm ci` and place each
`merged-binary.bin` in the matching `v1`, `v2` or `v3` build directory.
Then run:

```sh
node firmware/native/tools/package_installer.mjs firmware/native/build firmware/native/build/browser-installer
```

### Open the frame's web settings

Set a web settings password when saving over USB to enable the frame's
settings page. Use the IP address shown in the USB logs or find
`espframe-native` in your router's device list. Sign in as `espframe`
with the password you chose. The settings page can change the Immich
URL, API key and time per photo. Saving applies the changes on restart.
The page uses HTTP on the local network. Keep it on a trusted network.

Host regression checks run without the ESP-IDF toolchain:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/native/main \
  firmware/native/tests/swipe_test.cpp -o /tmp/espframe-swipe-test
/tmp/espframe-swipe-test
c++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/native/main \
  firmware/native/tests/slide_cache_test.cpp -o /tmp/espframe-slide-cache-test
/tmp/espframe-slide-cache-test
c++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/native/main \
  firmware/native/tests/photo_history_test.cpp -o /tmp/espframe-photo-history-test
/tmp/espframe-photo-history-test
c++ -std=c++20 -Wall -Wextra -Werror -Ifirmware/native/main -Ifirmware/native/tests/stubs \
  firmware/native/tests/recent_previews_test.cpp -o /tmp/espframe-recent-previews-test
/tmp/espframe-recent-previews-test
python3 firmware/native/tests/test_preview_decoder.py
node firmware/native/tests/serial_setup_test.mjs
```

## Connect to Immich with the ESP-IDF USB tools

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
loads additional photos directly from Immich. Each photo is fully visible
for 15 seconds by default before the next photo glides into place. A touch
pauses the glide at the frame already sent to the panel, and a swipe can
take it over in either direction. It keeps the current photo visible during a network
failure. The random feed continues as new previews become available. It
remembers the last 128 photo positions per session. It can decode up to
16 recent previews from a 2 MiB memory cache while offline, depending on
the sizes of the previews and available PSRAM. Older previews need Wi-Fi.

## Test on the frame

1. Boot to the middle scene and drag toward each adjacent scene slowly. The
   image boundary should follow your finger with no visible tearing.
2. Drag only a short distance and pause before lifting. The view should
   settle back. Repeat with a quick flick and check that it advances.
3. Start a transition, then grab it again and pull back. Repeat rapid
   forward and backward swipes, including the split scene.
4. Check the first and last scenes. Dragging beyond a ready boundary should
   move the photo slightly with resistance, keep the screen filled, and ease
   back when released. When a neighbor becomes ready mid-drag, continuing the
   gesture should move from its current position without a jump.
5. Record high-frame-rate video for touch-to-visible-motion and pacing.
   USB logs report recognition-to-DMA-scanout as `gesture_to_scanout_us`.
   This is an estimate before the new pixels are physically visible.
6. With Immich configured, wait for autoplay. Check the easing at either end
   of the glide. Press midway to pause it, then release without dragging.
   Repeat with a drag against the glide and compare touch response with an
   ordinary swipe. With a two-second interval, verify each photo is fully
   visible for two seconds before the next glide starts.

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
