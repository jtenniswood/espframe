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

When upgrading an existing frame, export an Espframe settings backup from
its web page before installation. In the native installer, choose that
backup to prefill the Immich URL, supported photo source, slideshow interval,
display mode and portrait-pairing preference. Backups that use filters the
native player cannot apply require you to choose a photo source before saving.
Enter the required Wi-Fi and Immich credentials in the setup form.

To build the same installer locally, use Python 3.10 or later, run `npm ci`,
and place each
`merged-binary.bin` and `espframe_native_vN.bin` in the matching
`v1`, `v2` or `v3` build directory. Then run:

```sh
node firmware/native/tools/package_installer.mjs firmware/native/build firmware/native/build/browser-installer
```

### Open the frame's web settings

Set a web settings password when saving over USB to enable the frame's
settings page. Use the IP address shown in the USB logs or find
`espframe-native` in your router's device list. Sign in as `espframe`
with the password you chose. The settings page can change the Immich
URL, API key, photo source, display mode, time per photo and portrait pairing.
Saving applies the changes on restart.
Under **Clock**, choose whether to show the local time and select the timezone
and 12- or 24-hour format. After time synchronization, the clock stays fixed
while photos move beneath it. New browser installs enable the clock by
default. Older native settings keep it hidden until enabled. Backups carry
the three clock settings. Time synchronization uses `pool.ntp.org`.
Use **Download settings backup** to save a copy of the active playback
settings for a future install.
The page uses HTTP on the local network. Keep it on a trusted network.

### Update the native firmware from the web settings

Download the installer artifact for the build you want to use. In the frame's
web settings, under **Firmware update**, select `v1-ota.bin`, `v2-ota.bin` or
`v3-ota.bin` for the revision shown, then select **Install update and restart**. Use
images from builds you trust. The package includes SHA-256 checksums for those
files. The prototype checks image integrity and board identity and accepts
unsigned firmware.

The frame writes to an inactive slot and restarts after validating the image.
The first boot must initialize the display, submit a frame and read the touch
controller. A failed trial returns to the previously bootable image. Wi-Fi
and Immich settings stay in the dedicated settings partition.

An older native prototype needs one USB install to add the update slots. Export
a backup first and leave the installer's full erase option unchecked to retain
its native settings. The OTA files are for the web settings and the installer
uses the corresponding factory images for USB.

Host regression checks run without the ESP-IDF toolchain:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/native/main \
  firmware/native/tests/swipe_test.cpp -o /tmp/espframe-swipe-test
/tmp/espframe-swipe-test
c++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/native/main \
  firmware/native/tests/swipe_input_test.cpp -o /tmp/espframe-swipe-input-test
/tmp/espframe-swipe-input-test
c++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/native/main \
  firmware/native/tests/slide_cache_test.cpp -o /tmp/espframe-slide-cache-test
/tmp/espframe-slide-cache-test
c++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/native/main \
  firmware/native/tests/slide_prefetch_test.cpp -o /tmp/espframe-slide-prefetch-test
/tmp/espframe-slide-prefetch-test
c++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/native/main \
  firmware/native/tests/photo_history_test.cpp -o /tmp/espframe-photo-history-test
/tmp/espframe-photo-history-test
c++ -std=c++20 -Wall -Wextra -Werror -Ifirmware/native/main -Ifirmware/native/tests/stubs \
  firmware/native/tests/recent_previews_test.cpp -o /tmp/espframe-recent-previews-test
/tmp/espframe-recent-previews-test
python3 firmware/native/tests/test_preview_decoder.py
node firmware/native/tests/serial_setup_test.mjs
node firmware/native/tests/photo_source_browser_test.mjs
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
for 15 seconds by default before a half-second crossfade. A touch holds the
frame already sent to the panel, and a swipe can take over from that frame in
either direction. A tap resumes the fade. Cancelling a drag settles back
and fades into the original photo. The player uses a glide when the pixel
accelerator or third display buffer is unavailable. It keeps the current
photo visible during a network failure. The random feed continues as new
previews become available. It
remembers the last 128 photo positions per session. It can decode up to
16 recent previews from a 2 MiB memory cache while offline, depending on
the sizes of the previews and available PSRAM. Older previews need Wi-Fi.
The frame reuses the Immich connection when the server allows it, and
reconnects if an idle connection has closed before the next response.
Eligible baseline JPEG previews use the ESP32-P4 JPEG decoder. The frame
uses software decoding for progressive JPEG and WebP, and falls back to
software if the hardware path cannot finish. Compare `decode_us` and
`hardware_jpeg` in the USB log when testing live playback.

Portrait pairing is on by default. From the second slide onward, the player
looks for a portrait from the same local capture day. A pair is composed
into one slide before it can be shown or swiped. If a new companion is
missing or slow to arrive, the primary photo fills the slide by itself.
**Show full image** is the default for the native prototype. **Crop to fit**
center-crops landscape photos to the screen and paired portraits to their
respective halves. A single portrait stays fully visible on a landscape frame.

The photo source defaults to **All photos**. Choose **Favourites** or
**Album** in the frame's web settings to limit both photos and their
portrait companions. For an album, paste its Immich link or ID and give the
frame's API key `album.read` permission. Album playback uses scoped random
search with Immich 3.2 or later. On earlier supported Immich versions it
plays the available photos in capture-date order, newest first. An empty or
unavailable selection leaves the current photo or startup scene visible.

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
   gesture should move from its current position without a jump. When a missing
   photo exists in that direction, the frame prepares it ahead of speculative
   photos. Try this in both directions on a slow connection, including a drag
   back to an older cached preview while a new photo is downloading.
5. Record high-frame-rate video for touch-to-visible motion and pacing.
   `gesture_to_scanout_us` in the USB log measures from the touch report
   that first moved the photo to the LCD DMA handoff. `compose_and_submit_us`
   is the part spent composing and submitting that frame. Capture both with
   cached photos and while the frame is downloading and decoding. Use the
   video to measure the physical touch and the first visible movement. The
   target is for 95% of cached gestures to move within 50 ms, with no visible
   tearing. Compare moving frames with the panel's measured refresh interval
   when checking pacing.
6. With Immich configured, wait for autoplay. Tap during the fade and check
   that it pauses and resumes smoothly. Grab it again, pull in each direction,
   and release after a short drag. The blended image should move with your
   finger until the gesture settles. Repeat while Immich is downloading or
   decoding a preview and check for visible tearing. With a two-second
   interval, verify each photo is fully visible for two seconds before fading.

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
