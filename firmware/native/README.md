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

For the experimental Guition V1 swipe renderer, use `-D SWIPE_BOARD=v1` and
`-D SWIPE_LINKED_SCANOUT=ON` in a separate build directory. It scans cached
photo rows directly during drags and slide motion after release. It draws the
clock and resisted edge into the writable frame. The build patches a local
copy of the ESP-IDF 6.1.1 DPI driver. The option defaults to `OFF` while
physical touch latency and tearing are being measured on the V1.

The player uses ESP-Hosted 3.1.0 on the P4 to talk to the frame's ESP32-C6
Wi-Fi radio. Espressif documents compatibility with C6 firmware from the 2.x
and 3.x generations. [ESP-Hosted migration guide](https://github.com/espressif/esp-hosted-mcu/blob/v3.1.0/docs/migration.md#compatibility)

The player only updates the P4. Use a fresh build directory if you previously
configured the project with ESP-Hosted 2.x.

When forward swipes run low on ready photos, the native player can start a
known landscape or portrait pair from Immich thumbnails. Fast refills stay
available for two seconds after a forward drag, including short pauses between
swipes. Reaching the forward edge can switch that slide's in-flight full
preview to thumbnails once, while the current photo stays visible. The slide
uses the existing display framing. Once you pause and the
next swipe is covered, the frame replaces the thumbnail slide with full
previews. Regular idle prefetch starts with full previews. Thumbnails that
fail to load fall back to previews, and a failed detail upgrade leaves the
original slide swipeable.

### Install and connect in a browser

Download and extract the `native-browser-installer` artifact from the Native
Swipe Prototype workflow. Open **Espframe Native Installer.html** in desktop
Chrome or Edge. The file contains the installer and all three factory images.
Select your frame revision and install the firmware. Close the installer dialog
and fill in Wi-Fi and Immich together. If you used Improv Serial to connect
Wi-Fi in the dialog, select **Use Wi-Fi already saved on the frame** and fill
in only the remaining settings. Select the frame's USB port to save.
Keep the USB cable connected while the frame restarts. The installer
shows the frame's address and an **Open frame settings** link.
Use **Find frame on Wi-Fi** to retry the address check.
If the frame stays offline or you change routers, open **Change Wi-Fi over USB**.
This updates the network and keeps your saved Immich and playback settings.

When upgrading an existing frame, export an Espframe settings backup from
its web page before installation. In the native installer, choose that
backup to prefill the frame name, Immich URL, supported photo source, slideshow interval, connection timeout,
display mode, portrait pairing, mounting orientation, clock, brightness, and
screen schedule. Recognized ESPHome backups prefill **Fill each half** for
portrait pairs, independently of the display mode for single photos.
Backups that use filters the
native player cannot apply require you to choose a photo source before saving.
To keep saved connections, leave **Erase device** unchecked during firmware
installation. In the setup form, select **Keep the Wi-Fi connection from
ESPHome** or **Keep the Immich connection from ESPHome**. The frame reads the
selected connection directly from its retained settings. It keeps the Immich
URL and API key together. If a connection is unavailable, clear its checkbox
and enter the details in the form.

For migration acceptance, export a second backup from the native frame after
setup. Run this comparison from the repository root with the original
ESPHome backup and the new export.

```sh
node firmware/native/tools/compare_migration_backups.mjs before.json after.json
```

The comparison checks settings recognized by the installer, including the
filter defaults applied when restoring a photo source. It reports missing or
changed fields by name and exits with an error when it finds a
difference or an import notice that needs review. Use the original backup
again to check the settings after restoring ESPHome. Verify Wi-Fi, Immich,
and Home Assistant by confirming the frame reconnects to each after the
firmware change.

To build the same installer locally, use Python 3.10 or later, run `npm ci`,
and place each
`merged-binary.bin` and `espframe_native_vN.bin` in the matching
`v1`, `v2` or `v3` build directory. Then run:

```sh
node firmware/native/tools/package_installer.mjs firmware/native/build firmware/native/build/browser-installer
```

The package also includes `index.html` and its assets for serving the same
installer over HTTPS or localhost.

### Prepare a versioned Native Preview

Run the **Native Swipe Prototype** workflow on the intended source commit
with `preview_version` set to a version such as `v0.1.0-preview.1`. The
`preview_v1_linked_scanout` option selects the experimental V1 renderer
for a hands-on comparison. It defaults to off. The workflow produces the
`espframe-native-v0.1.0-preview.1` artifact with the standalone browser
installer, factory and OTA images, SHA-256 checksums, and the source commit.

For a local build set `-D NATIVE_PREVIEW_VERSION=v0.1.0-preview.1` in each
native factory build. Pass the same version as the third argument to
`package_installer.mjs`. The packager checks that every image embeds the
requested version before creating the installer.

Open **Espframe Native Installer.html** in desktop Chrome or Edge. The
stable ESPHome installer provides the return path. Keep a backup and leave
**Erase device** unchecked when switching firmware to retain the saved
ESPHome preferences.

### Firmware releases and automatic updates

Open **Firmware** in the frame settings to see the installed Native Preview,
check for a release, install an available version, or choose a previous one.
Enable **Auto Update** and choose **Update Frequency** to install newer releases
from the frame itself. Turning **Auto Update** back on checks for a release
immediately. **Check for Update** refreshes the list and leaves installation to
you. The frame verifies the board and download checksum before
restarting into the update. The native startup check can return to the previous
firmware if startup fails.

To publish a preview, prepare a GitHub draft prerelease on the matching
`native-v0.1.0-preview.N` tag. Dispatch **Native Swipe Prototype** on that tag
with `preview_version` set to `v0.1.0-preview.N` and **Publish a verified draft**
enabled. A successful publish updates the Native Preview feed on GitHub Pages.
The feed serves the most recent five published previews for each board.

### Open the frame's web settings

Use **Find frame on Wi-Fi** in the browser installer or open the IP address
shown in the USB logs. The frame uses its stock board hostname when its name
is blank. The settings page opens without a password by default. If you set
an optional password over USB, sign in as `espframe` with the password you
chose. **Remove saved web admin password over USB** clears a saved password
and returns to direct access. The settings page uses Espframe's shared web
controls and shows the settings supported by the running native firmware.
Edit the Immich connection, display mode, time per photo, and other
playback and screen settings, then choose **Save settings**. Changes limited
to framing or captions redraw the current photo or portrait pair and keep
its place in the slideshow. Cached neighbors stay swipeable while their
framing and captions refresh in the background. Changing photo selection or
pairing rules keeps the current photo visible until the first slide from
the new feed is ready.
Clock visibility, format and timezone update in place. A timezone change
also updates sunrise, sunset and the screen schedule. The next automatic
photo change uses the saved transition and speed.
The connection timeout applies immediately. It controls when an Immich
connection or API key warning appears while the frame waits for its first
photo or reconnects with new settings. The last ready photo stays visible
during reconnection.
Screen tone changes refresh visible and cached photos through the same path
as framing. **Warm tone until sunrise** applies the configured nighttime warmth
and turns off once the frame's clock and timezone place it in daylight.
Immich URL and API key changes reconnect during playback. Device settings
such as the frame name apply after a restart.
If you change **Frame name**, it also shows the new `.local` address.
The frame adds its own four-character MAC suffix to the hostname.
Under **Clock**, choose whether to show the local time and select the timezone
and 12- or 24-hour format. After time synchronization, the clock stays fixed
while photos move beneath it. New browser installs enable the clock by
default. Older native settings keep it hidden until enabled. Under
**Clock → Advanced**, set up to three NTP hosts in preference order. Backups carry the
clock settings and time servers. Existing native setups use `pool.ntp.org`
until you set a server. Leaving all three fields blank selects the same
default time-server pool as ESPHome. Changing the servers restarts time
synchronization while the frame keeps running.
The frame address opens the **Immich**, **Device**, and **Logs** tabs.
Use **Backup** in the **Device** tab to export or restore your settings.
Use **Immich → Filters → Photo source presets** to select All photos,
Favourites, a standalone album by link or ID, On This Day, or Custom filters.
Combine filters in **Filters** and adjust On This Day in **Memories**. Choose
**Custom** in **Frequency** or **Night Schedule** to enter an exact duration.
Use **Device → Firmware → Upload firmware file** to install a local image.
Under **Firmware → WiFi firmware**, see the current and latest compatible
Wi-Fi chip firmware, check manually, or install an available update. The
separate **Auto Update** switch applies to the Wi-Fi chip and is included
in settings backups. The frame checks the ESPHome Wi-Fi firmware feed after
connecting and then daily.

Use **Immich → Connection** to configure [WireGuard](../../docs/wireguard.md).
After changing the tunnel settings, choose **Restart now** to apply them. The frame keeps the private key
write-only and excludes WireGuard settings from web backups.
The page uses HTTP on the local network. Keep it on a trusted network.

The native player also answers the shared configuration API at
`/espframe/api/v1/capabilities` and `/espframe/api/v1/configuration`.
Authenticated reads report common settings and list unavailable fields.
Same-origin writes accept the existing form-encoded configuration payload.
The player saves the supported fields together. The update response reports
whether applying the save requires a restart.
The API key is write-only. Under **Device → Factory Reset**, use
**Partial reset** to clear both photo connections and restore playback
and display defaults. The frame retains Wi-Fi and the web settings password,
so you can reconnect Immich from the main admin after it restarts. The shared
configuration API reports the new reset epoch and rejects stale writes that
carry an earlier epoch. A complete reset also clears Wi-Fi and returns the
frame to first-time setup.
Changes to independent filters through the shared API apply to playback
immediately. The frame uses the active album as the starting album filter
when no album list has been saved, and carries over an active Favorites
selection. On This Day keeps running while independent filter changes are
saved for later.

### Connect Home Assistant

In browser setup, expand **Home Assistant** and enter a unique ESPHome
encryption key when saving over USB. In Home Assistant, open **Settings →
Devices & services** and configure the discovered ESPHome device with the same
key. You can also add it manually by IP on port 6053. Use a 32-byte Base64 key as
described in the [ESPHome native API documentation](https://esphome.io/components/api/).

The native player uses the stock entity IDs for **Screen: Backlight**,
**Screen: Sleep**, **Screen: Wake**, and **Photos: Slideshow Interval**.
Existing dashboards can also use **Network: Online**, **Network: WiFi Strength**,
and **Network: IP Address** after migration. Wi-Fi strength refreshes once a minute.
The four stock **Memory** sensors report free internal heap and PSRAM as
percentages, and the largest byte-addressable block in each region in bytes.
The frame samples those values once a minute while Home Assistant subscribes.
**Device: Reboot Screen** restarts the frame, and **Reset Reason** reports what
caused the last startup. A reboot request takes effect when the frame is
running a confirmed image and no firmware update is in progress.
The backlight light entity shows the current screen state and brightness.
Turning it off puts the frame to sleep. Turning it on respects scheduled off-hours.
Use **Screen: Wake** for a temporary wake during off-hours. A brightness
change lasts until the next sleep, explicit wake, or scheduled brightness change.
Interval changes from either the frame's web settings or Home Assistant take
effect during playback. Home Assistant shows the current interval and the stock
choices. It picks up a new custom choice the next time it reads the entity list.
**Device: Language** updates display text and photo dates. **Screen: Connection Timeout**
sets how long the frame waits for its first photo before showing a connection warning.
Both controls use the stock entity identities and choices. Changes apply immediately
and are shared with the frame's web settings.
**Photos: Source** keeps the stock presets. Source changes keep the displayed
photo visible and advance to the new source as soon as its first slide is ready.
A preset uses the saved Immich IDs for that source from the frame's settings.
The installer keeps the active preset when importing an Espframe backup.
Changes made from the frame's web settings also update an active Home Assistant session.
A frame without a custom name advertises its stock
MAC-suffixed hostname when Home Assistant is enabled. The key stays on the
frame across native OTA updates and customization resets. Use **Remove saved
Home Assistant key** in browser setup or factory reset to clear it. Settings
exports and responses exclude the key.
To keep a stock ESPHome pairing during USB migration, export your settings
backup and leave **Erase device** unchecked when installing native firmware.
Then choose **Keep the existing ESPHome pairing** in browser setup and save
over USB. The frame copies the retained key into native settings locally.
If the native settings and installer leave the frame name unspecified, the
frame also copies the saved ESPHome name so Home Assistant can use the same
hostname. A name supplied by the installer or backup takes precedence.
If the stored pairing is unavailable, setup reports that before saving.
You can also enter an existing key manually. Keep the matching stock
installer available if you need to return to ESPHome.
Removing the native key or running a native factory reset blocks reimport.
The retained stock copy remains available for ESPHome recovery until a full
flash erase.

### Update the native firmware from the web settings

Open **Device → Firmware** to check for and install published Native Preview
releases or configure automatic updates.

Download the installer artifact for the build you want to use. Open
**Device → Firmware → Upload firmware file**, select `v1-ota.bin`, `v2-ota.bin` or
`v3-ota.bin` for the revision shown, then select **Install update and restart**. Use
images from builds you trust. The package includes SHA-256 checksums for those
files. The prototype checks image integrity and board identity and accepts
unsigned firmware.

To install a local build over Wi-Fi, use the native OTA API. Verify the
SHA-256 checksum of the accompanying `v1-ota.bin`, `v2-ota.bin`, or
`v3-ota.bin` for your board and use an image from a build you trust. Set
`FRAME_URL` to the frame’s HTTP address and use the matching file.

```sh
curl --fail-with-body -H "Origin: $FRAME_URL" \
  -H "Content-Type: application/octet-stream" \
  --data-binary @v1-ota.bin "$FRAME_URL/api/firmware"
```

Add `--user espframe` if the frame has a web settings password. The frame
validates the image and board identity, writes to the inactive slot, and
restarts. After it reconnects, read `GET /api/firmware` and check that
`running` matches the upload response’s `target` and `boot_verified` is
true. Wi-Fi and Immich settings stay in the dedicated settings partition.
The installer keeps ESPHome’s update preferences in native backups for a
future restore to ESPHome.

#### Test an interrupted upload and failed startup

Use a dedicated Guition V1 with USB recovery available. Download the
`native-v1-ota-recovery-test` artifact from Native Swipe Prototype and
check its `SHA256SUMS`. This test uses a separate firmware project name so
the recovery images are kept outside regular installer packages and updates.

1. Install `native-ota-test-v1-factory.bin` to offset zero over USB. Use the
   usual native USB settings form to connect the test frame and set its web
   settings password. Export a settings backup for comparison.
2. From a computer on the same network, run the following from the repository
   root. Enter the frame's web settings password when prompted.

   ```sh
   python3 firmware/native/tools/interrupt_ota_test_upload.py "http://$FRAME_IP" native-ota-test-v1-baseline.bin
   ```

   The tool lets a partial transfer time out and verifies that the original
   image is still serving settings.
3. Upload `native-ota-test-v1-baseline.bin` with the OTA API command above.
   Verify that `GET /api/firmware` reports the new image as verified. This
   places a verified image in the first OTA slot.
4. Upload `native-ota-test-v1-fail.bin` through the same API. This image
   leaves its startup check pending, then the normal timeout requests
   rollback. Verify that `GET /api/firmware` reports the baseline image
   running, the slideshow and touch work, and the exported settings match
   the earlier backup values.
5. Restore the test frame with its regular native or ESPHome installer over USB.

To build the pair locally, configure the native firmware with
`-D SWIPE_NATIVE_OTA_TEST=BASELINE` and run `build merge-bin` for the factory
and baseline app images. Configure a separate build directory with
`-D SWIPE_NATIVE_OTA_TEST=FAIL` and run `build` for the failing app image.
Both builds use `espframe_ota_test_vN.bin` for their app filenames.

### Recover Wi-Fi from the frame

When the frame has no saved Wi-Fi, use the USB installer or join the open
`espframe_` network shown on the frame from a phone or laptop. The Wi-Fi
setup page opens automatically on supported devices. You can also visit
`http://192.168.4.1` directly. Choose a nearby Wi-Fi network or enter its
name manually, then enter its password. The frame tests the connection
before saving the credentials. Rejoin your usual Wi-Fi and open the address
shown on the frame and in the setup page to continue. The frame keeps its
Immich Setup screen and browser directions visible until you save an Immich
server URL and API key.

A configured frame opens a password-protected recovery hotspot after 90
seconds without a home-network address. The frame shows its network name and
password over the photo when the screen is awake and idle. Tap the screen
to wake it. Cached photos remain available to swipe. When Wi-Fi is restored,
the hotspot stays up for 15 seconds so the browser can show the new address.

An older native prototype needs one USB install to add the update slots. Export
a backup first and leave the installer's full erase option unchecked to retain
its native settings. The OTA files are for the web settings and the installer
uses the corresponding factory images for USB.
The packager verifies each factory image's partition table against the native
layout. The layout reserves native settings separately and maps the retained
ESPHome settings read-only.

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
node firmware/native/tests/shared_admin_browser_test.mjs
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
and fades into the original photo. At the end of the ready photos, a forward
drag meets resistance. Releasing there settles back to the last ready photo,
and the frame prioritizes the next slide and refills ahead for the next swipe.
The player uses a glide when the pixel
accelerator or third display buffer is unavailable. It keeps the current
photo visible during a network failure. The random feed continues as new
previews become available. It
remembers the last 128 photo positions per session. It reserves six decoded
slide slots and can add up to three when PSRAM is available after display
startup. Prefetch favors the last swipe direction and uses spare slots for
upcoming photos when there is little history. The V1 trial reports the
allocated slots and ready next photos as `cache_slots` and `ready_ahead`.
It can decode up to
16 recent previews or prepared slide JPEGs from a shared memory cache while
offline, depending on file sizes and available PSRAM. The cache can use up to
2 MiB for direct Immich, or 4 MiB once a prepared slide has loaded. A prepared
pair occupies one entry. Older photos may need another download.
The frame reuses the Immich connection when the server allows it, and
reconnects if an idle connection has closed before the next response.
While decoding a direct Immich slide, it can start downloading the next
known preview if enough PSRAM is available. The lookahead holds one preview
up to 1 MiB. When a portrait companion is already known, the worker starts it
while decoding the first portrait. A missed or oversized lookahead uses the
normal download path. A required preview can use the normal transfer budget
on its existing request. If that wait runs out, the frame uses its usual retry
delay. Optional companions keep the shorter pairing deadline.
Wi-Fi association and time synchronization run independently of photo
preparation, so the worker can prepare cached previews during reconnect.
The native frame scans all channels at connection time and tries the
strongest access point for the saved Wi-Fi network first. It disables
Wi-Fi modem sleep to keep receive latency low.
When connectivity returns, a new connection gets priority over a pending
photo retry delay. If Wi-Fi disconnects or the frame gets a different IP,
the next Immich request uses a fresh HTTP connection.
Eligible baseline JPEG previews use the ESP32-P4 JPEG decoder. The frame
uses software decoding for progressive JPEG and WebP, and falls back to
software if the hardware path cannot finish. The software JPEG decoder can
downscale in eighths during decoding and uses the smallest size that covers
the selected crop. Progressive JPEGs skip transforms outside the crop's top
and bottom edges. WebP decoding can pause between row batches while a drag
needs the display's memory bandwidth. Compare
`decode_us` and `hardware_jpeg` in the USB log when testing live playback.

Portrait pairing is on by default. From the second slide onward, the player
looks for a companion within the selected local capture date range.
Choose **Same Day**, **Within 1 Day** or **Within 2 Days**. A pair is composed
into one slide before it can be shown or swiped. If a new companion is
missing or slow to arrive, the primary photo fills the slide by itself.
Enable **Show paired portraits only** to skip a portrait when its companion
is unavailable. Landscape photos still appear as single slides. Turning off
portrait pairing pauses this filter and retains
its saved value and range. Native setup and Espframe settings backups carry
both options.
New setups default to **Crop to fit**. This center-crops a single photo to
fill the screen, including a portrait shown alone. With **Follow photo framing**,
it also crops paired portraits to their respective halves. **Show full image**
keeps the whole photo visible. Running USB setup again uses the frame's saved
framing unless you pick a different option or import a backup with framing settings.
Set **Portrait pair framing** to **Fill each half** to crop both portraits
independently of single-photo framing. **Fit both full photos** keeps both
portraits intact. **Follow photo framing** retains the previous behavior.
Set **Mounting orientation** to **Upside down (180°)** when the frame is
mounted the other way round. Photos, portrait pairs, the clock,
and touch follow the new orientation together. Espframe backups carry the
landscape mounting choice.

To mount the frame vertically, open the admin with `?developer=experimental`
and enable in-development features under **Device**. The **Rotation** card
then offers 90 and 270 degrees. Portrait rotation uses one photo per slide.
Turning off in-development features restores the default landscape orientation.

Under **Photo captions**, enable the capture date, location, or both. The
frame uses the photo's local capture day and the city and country supplied by
Immich. Both right-aligned lines use 28-pixel Rubik Regular at weight 400.
Long locations shorten to leave room for the clock when it is enabled.
The clock uses 150-pixel Rubik Light at the lower left, with equal-width
slots for the digits. The clock and captions use precomputed soft drop
shadows to improve readability on bright photos.
When two portraits are paired, the caption describes the primary
photo. The caption is composed with the cached slide, so dragging and
swiping need no extra image, text, or network work. The frame can show the
date in either date-taken format or as a relative age in the configured
timezone. **Display language** uses Espframe's 22-language catalogue for
dates, Wi-Fi recovery, loading, and photo source and connection notices.
The installer restores this choice from an Espframe backup. Switching
languages redraws cached captions in the background. Before time sync the
frame shows the capture date. Once time is available, the player refreshes
cached captions in the background.
When changing status translations, rerun `firmware/native/tools/rasterize_status.py`
with Pillow, `python-bidi==0.6.6`, and the Noto Sans and Material Design Icons
font faces recorded in `product/source-ownership.json`. Running it with just the output path checks
that the shipped masks match the current catalogue.
Turning on relative captions, a relative capture-date filter, or Memories
requests time sync even when the clock is hidden.

Location glyphs missing from Rubik use Noto Sans Regular to retain Latin,
Greek and Cyrillic coverage. When a name cannot be rendered, the frame can
still show the other location field. These settings are included in the
native player's backups.

The photo source defaults to **All photos** from the Immich timeline. Choose **Favourites** or
**Album** in the frame's web settings to limit both photos and their
portrait companions. For an album, paste its Immich link or ID and give the
frame's API key `album.read` permission. Album playback uses scoped random
search with Immich 3.2 or later. On earlier supported Immich versions it
plays the available photos in capture-date order, newest first. An empty or
unavailable selection leaves the current photo or startup scene visible.

For All photos, Favourites, or Album, enable **Filter by Date** to
limit playback and portrait pairing by the photo's local capture date.
Fixed ranges include both endpoints, and either end can be blank. Relative
ranges use the frame's local day and wait for the clock to synchronise.
The worker updates the range when it next prepares photos after midnight.
Already prepared photos and swipe history stay available. Both ranges are
saved in Espframe backups and are ignored during On This Day and its fallback.

Choose **On This Day** to play Immich memories from prior years. Pick a
window from **Same Day** to **±7 days**, with **±2 days** as the default. The
frame needs local time and an API key with `memory.read` permission. The
worker begins with today's date, checks neighbouring dates as slides are
needed, and keeps a bounded sample available for replay. Portrait companions
for memories come from those candidates.

If the selected window is empty or Immich cannot answer the request,
**Fall back to all photos** continues with the full library. With fallback
off, the idle frame keeps its current photo and shows a status message when
it runs out of prepared memories. Backups preserve the source, date window,
fallback choice, and a saved single album.

Set **Daytime brightness** and **Nighttime brightness** in the USB installer
or the frame's web settings. Both accept 10–100%. New browser installations
default to 64% by day and 40% at night. Older native installations keep their
existing level until a separate nighttime level is saved. Espframe backups
can restore both levels. The frame estimates sunrise and sunset from the
selected timezone and uses the daytime level until it has valid local time.
At locations without a sunrise or sunset on that day, the corresponding
fallback is 06:00 or 18:00.

**Screen tone** lets you add base warmth to photos and additional warmth at
night. The night amount ramps up in the hour before sunset and ramps back down
in the hour after sunrise, using the frame's timezone. Before the clock is
synchronized, photos use the base setting. Tone uses the same RGB565 colour
curve as the ESPHome player and leaves the caption and clock colours alone.

The worker applies the tone when preparing each photo or portrait pair. Cached
slides keep the tone they were prepared with until the next preparation, so a
swipe or crossfade can use ready pixels. Base and night settings transfer in
Espframe backups.

Hold one finger still on the screen for at least three seconds and release
to turn off the backlight. Touch the screen to wake it to the same photo at
the current brightness. Autoplay starts a fresh interval after you lift your
finger.

Enable **Screen schedule** to keep the display on between two local hours.
It uses the configured timezone even when the clock is hidden. Overnight
windows can span midnight, and matching hours leave the screen on all day.
Outside the window, tap to wake temporarily. The default wake duration is
60 seconds. If you're swiping when the timer ends, the screen waits for the
gesture to settle. A manual hold-to-sleep keeps the frame dark until you tap.
The schedule takes effect once the frame has valid local time. Its settings
carry over in Espframe backups.

## Test on the frame

### Temporary V1 swipe trial from ESPHome

The V1 trial is a separate app-only build that uses the 16 MiB ESPHome
partition layout. It boots into the cached sample slides for three minutes
and then returns to the confirmed ESPHome app in the other slot. Wi-Fi, USB
setup and Immich loading are disabled in this trial. The trial does not read
or change saved frame settings. It uses the same touch and rendering path as
the normal player and publishes timing logs on the USB console.

```sh
idf.py -C firmware/native -B firmware/native/build/esphome-trial-v1 \
  -D SWIPE_BOARD=v1 -D SWIPE_ESPHOME_TRIAL=ON -D SWIPE_TRACE=ON build
```

The artifact `espframe_v1_legacy_swipe_trial.bin` is only for an ESPHome app
OTA upload on an original Guition V1. Verify the installed ESPHome partition
table, rollback-capable bootloader, and working firmware in the active slot
before uploading. Have USB recovery available for the first hardware trial.
Use only the app binary. The regular browser installer and native settings
updater expect the regular native firmware.

To check an ESPHome device through its browser, build
`builds/guition-esp32-p4-jc8012p4a1.native-trial-readiness.yaml` with
the same ESP-Hosted version as the running frame. Upload its ESPHome app
over OTA. The **Native V1 swipe trial readiness** diagnostic reports
**Ready** once the bootloader has put this boot in pending verification
and ESPHome has confirmed the app. It also verifies the partition layout
and the other OTA target. Read the result before any other reboot or
update, since a normal reboot of a confirmed app cannot prove rollback.
Keep the working ESPHome OTA binary and USB recovery available.

#### Check boot readiness for automatic migration

The optional `builds/guition-esp32-p4-jc8012p4a1[-v2|-v3].native-migration-readiness.yaml`
builds exercise the same check on all three board revisions. Build the file
matching the frame and use the same ESP-Hosted version as the installed
ESPHome build. On a dedicated ESPHome test frame, upload just the diagnostic
app over OTA and read **Native migration boot readiness** before rebooting
again. **Ready** means the board's ESPHome app, flash layout and alternate OTA
slot match, and this boot demonstrated rollback support and confirmed the app.
The diagnostic build keeps running ESPHome. Settings compatibility and
successful native startup still need separate checks before migration.

#### Test the automatic V1 handoff locally

Build an app for the legacy ESPHome slots with ESP-IDF:

```sh
idf.py -C firmware/native -B firmware/native/build/legacy-v1 \
  -D SWIPE_BOARD=v1 -D NATIVE_LEGACY_MIGRATION=ON build
```

Serve `espframe_legacy_v1.bin` from a test host on the frame's network. Build
`builds/guition-esp32-p4-jc8012p4a1.native-migration-bridge.yaml` with
`native_migration_enabled=true`, the local `native_migration_url`, and the
candidate's `native_migration_md5`. Use an OTA app upload for the bridge on the
dedicated V1 running stock ESPHome. Keep the original firmware and USB
recovery available.

The bridge checks Auto Update, active playback, and rollback readiness before
it snapshots the current stock settings and fetches the pinned candidate.
The snapshot stays on the device. The native build reads the retained Wi-Fi,
Immich, and Home Assistant records and stores its settings in a separate NVS
namespace. It confirms the boot after displaying an Immich photo and receiving
a valid touch-controller report. The bridge can retry transport failures on
the same confirmed ESPHome boot. A rollback starts a new boot and the bridge
requires a fresh rollback-capable ESPHome boot before it can migrate again.
Frames with configured WireGuard stay on ESPHome.

For the full private rehearsal, build
`builds/guition-esp32-p4-jc8012p4a1.native-migration-rehearsal.yaml` with
`firmware_manifest_url` pointing at a LAN ESPHome release manifest whose OTA
entry is the bridge. Give the bridge a greater stable version and point its
manifest at the same LAN URL. Load the stock-equivalent rehearsal factory
image on the V1 and let its ordinary auto-update component install the bridge.

Build the native candidates with distinct `NATIVE_PREVIEW_VERSION` values
and the same `NATIVE_LEGACY_REHEARSAL_FEED` ending in `/`. The private feed
uses the native preview index shape and SHA-256 verified app downloads.
Expose the next candidate only after the previous boot is confirmed. The
private build checks on a short cadence and allows a startup failure fixture
with `NATIVE_LEGACY_REHEARSAL_FAIL=ON`. Keep the failure fixture on the test
feed and verify that the previous app returns without repeatedly staging
the rejected image. Public firmware feeds remain separate from the rehearsal.

#### Test live Immich over Wi-Fi

For a V1 configured through the stock ESPHome installer, build the
five-minute Immich trial against the same app slots. It reads the saved
Wi-Fi and Immich connection from the ESPHome 2026.11 preference format.
The trial keeps the saved date range, portrait-pairing settings and
mounting orientation. It also uses the saved stock frame name for DHCP
when connecting to Wi-Fi. The trial returns immediately to ESPHome if
its required settings are missing or invalid, or an active photo filter
is outside that scope.

```sh
idf.py -C firmware/native -B firmware/native/build/esphome-live-trial-v1 \
  -D SWIPE_BOARD=v1 -D SWIPE_ESPHOME_TRIAL=ON -D SWIPE_TRIAL_IMMICH=ON -D SWIPE_TRACE=ON build
```

After the readiness diagnostic reports **Ready**, upload only
`espframe_v1_live_trial.bin`. When the frame joins Wi-Fi, `/api/trial`
reports the count of prepared Immich slides, time to prepare the first
photo, the preview requests needed for the first slide, and
touch-report-to-LCD-handoff timing for the most recent swipe.
For a hands-on session, open `/api/trial/benchmark` from another device
after dragging the photos and save its response before rollback. The response
shows `"scripted":false` and summarizes the first motion, drag gaps and
cache hits from physical gestures since boot. The cached first-motion
samples are split by whether a preparation job is in flight. Drag gaps
are split when both frames are in the same workload, and gaps across a
workload change still count in the overall total. Its `physical_drags` counts
report forward and backward landings, returns to the starting photo,
cancellations during a recognized drag, and released drags interrupted
before their final frame reached the display. Taps and gestures that
never start a horizontal drag are outside these counts. Drag continuously
to assess pacing, since a pause mid-drag can also widen the gap between
display updates.
The live and rapid trials use the native player's forward refill policy.
The frame can prepare the next landscape or portrait pair from thumbnails
when forward gestures run low on cached photos. The benchmark reports
`live_edge_detail`. The status route reports the number of thumbnail slides,
how many were portrait pairs, how many full previews were switched to
thumbnails at the edge, the number of full-detail LCD handoffs, and the last
handoff time. A short run with a warm cache can finish with no thumbnail
slides.

Use high-frame-rate video to measure from finger movement to visible motion
and check the panel for tearing.
The status route also reports clock and connection readiness, HTTP status
codes, and candidate counts. `candidate_retries` counts times a preview was
rejected and another candidate was already available, so the player could
retry that slide after 100 ms. `pixel_pauses` and `pixel_pause_ms` report how
often and how long pixel preparation waited during a swipe. Transfers can
continue during a swipe. At an unready edge, pixel work yields during the
bulk copies in the first four refresh periods of a drag. The renderer wakes
the loader when it releases the pause. It can resume between copies, during
a stationary hold, or after six refresh periods of dragging toward the last
ready neighbour. The status includes the Wi-Fi
power-save mode, signal level and primary channel when the frame gets an address.
HTTP diagnostic counts distinguish
connection-open failures from retryable header and body timeouts. The loader
uses 250 ms receive timeouts to check for cancellation during stalled reads.
These counts include waits within requests that later finish, and each request
has a separate total time budget.
`/api/trial/prepare` gives the count, total, and worst elapsed time for
whole preparation attempts, photo and pair selection, preview transfers,
decode by format, and panel composition. Transfer entries include received
bytes. Attempt time includes failed attempts and overlaps the other stages.
Decode and composition timings include time waiting for a pixel checkpoint,
which `/api/trial` reports separately as `pixel_pause_ms`.
For completed progressive JPEG decodes, `progressive_jpeg_input` measures
loading and processing the JPEG scans. `progressive_jpeg_output` measures
transforms, color conversion, cropping, and scaling into the output buffer.
Each reports its own accumulated `checkpoint_us` within the elapsed time.
ESPHome returns automatically after five minutes.

#### Measure swipe rendering over the local network

The separate V1 benchmark runs 48 scripted drags in both directions through
real Immich photos. It waits for the neighbouring slide to be in the cache
before each drag and continues far enough for the preparation worker to fetch
new photos. The usual touch reader runs, and the scripted reports take the
place of physical reports for this five-minute trial.

```sh
idf.py -C firmware/native -B firmware/native/build/esphome-benchmark-v1 \
  -D SWIPE_BOARD=v1 -D SWIPE_ESPHOME_TRIAL=ON -D SWIPE_TRIAL_IMMICH=ON \
  -D SWIPE_TRIAL_BENCHMARK=ON build
```

Use the same ESPHome rollback readiness check before uploading the app-only
`espframe_v1_swipe_benchmark.bin`. The CI artifact is named
`native-v1-esphome-swipe-benchmark`. While the trial is active,
`/api/trial/benchmark` reports landed destinations, cache hits, the DMA2D
burst size, DMA2D and CPU bulk copy counts, and the number and worst duration
of first movements and drag gaps. First-movement timings report slide
composition and display submission separately. Composition includes the
slide copies and overlays. Submission includes the LCD driver's cache
writeback. The route counts
movement-to-LCD-handoff samples over 50 ms and drag gaps beyond one and a
half nominal refresh periods. The busy counts cover first motion submitted
while a preparation lease was active and drag gaps whose two ends overlapped
preparation. `drag_handoff_delay` counts how long the renderer took to observe
each drag frame's LCD DMA handoff, including the number of waits over 250 µs.
`in_range_drag_gaps` measures movement among ready slides and
`resisted_drag_gaps` measures movement outside that range. A gap spanning
both paths is included in the overall count. `settle_gaps` measures the snap-back
or landing animation after release. The `in_range_settle_gaps` and
`resisted_settle_gaps` fields distinguish those same two paths. `landing_gaps`
measures the interval ending at the destination, and `approach_gaps` measures
earlier intervals in that animation.
`in_range_drag_updates` and `resisted_drag_updates` measure from when the
renderer first sees a new pixel
position to the handoff that satisfies it. They omit intervals when a hold or
rounding at the edge produces the same position. Each reports the number of
updates that waited longer than one and a half nominal refresh periods.
`split_blits` reports the time
to copy both slices for frames that show two photos and counts how often
both copies were queued together.
Save the response before the automatic return to ESPHome.

To test faster browsing, build the same live trial in a separate directory
with `-D SWIPE_TRIAL_BENCHMARK_RAPID=ON` and upload
`espframe_v1_rapid_benchmark.bin` with the same rollback readiness check.
It starts the next gesture 50 ms after the landed frame and lets forward
swipes reach an uncached neighbour. It also reverses toward cached photos.
The report adds the number of cold starts, releases before the neighbour
was ready, advances after a cold release, cold gestures that settled back,
and elapsed time from the first swipe to the last settle. It tracks first motion on
cache misses and measures the time from the first press toward each missing
neighbour until that slide becomes ready in the swipe range. Repeated
presses toward the same missing photo keep the original start time.

Set `-D SWIPE_TRIAL_BENCHMARK_FIXTURES=ON` in a separate build directory for
a local-fixture control. Upload `espframe_v1_fixture_benchmark.bin` with the
same readiness check. It connects to Wi-Fi for the report and uses the same
display buffer setup. Its report names `fixtures` as the photo source and
ESPHome returns after two minutes.

The measurements start at a touch report and end at an LCD DMA handoff.
Use the regular live trial with physical touch and high-frame-rate video to
check sensor response, visible animation pacing, and tearing.
To compare the detail handoff on the same photos, use the
[physical-touch replay](prepared-slides/README.md#compare-the-handoff-with-physical-touch).

#### Verify cached swipes during a Wi-Fi outage

Build the standard live benchmark in a separate directory with
`-D SWIPE_TRIAL_WIFI_OUTAGE=ON`. After the ESPHome rollback readiness check,
upload the app-only `espframe_v1_wifi_benchmark.bin` from that build or the
`native-v1-esphome-wifi-outage-benchmark` CI artifact.

The frame warms at least three forward slides, disconnects its own Wi-Fi
station, and holds reconnection for 12 seconds after the driver reports the
disconnect. It alternates six swipes through cached photos while offline.
The normal reconnect loop then resumes. The script waits for photo access
and walks forward 12 slides, past the range that was ready during the outage.
The HTTP report becomes reachable again after Wi-Fi recovers.

Before ESPHome returns, save `/api/trial/benchmark` and check that `wifi_phase`
is `reconnected`, `offline_settled` is 6, `reconnected_settled` is 12,
`wrong_slide` is 0, and `settled_slide` exceeds `offline_ready_last`.

#### Verify the rollback and gestures

The demo checks the running partition map and the previous ESPHome app before
starting the display. The bootloader must mark the demo pending verification.
The demo never confirms its boot, so a reset during it should also go back to
ESPHome. On hardware, verify both the timed return and the return after a
reset, then confirm the original frame reconnects and retains its settings.

1. Boot to the middle scene and drag toward each adjacent scene slowly. The
   image boundary should follow your finger with no visible tearing.
2. Drag only a short distance and pause before lifting. The view should
   settle back. Repeat with a quick flick and check that it advances.
3. Start a transition, then grab it again and pull back. Repeat rapid
   forward and backward swipes, including the split scene.
4. Check the first and last scenes. Dragging beyond a ready boundary should
   move the photo slightly with resistance, keep the screen filled, and ease
   back when released. When a neighbor becomes ready mid-drag, continuing the
   gesture should move from its current position without a jump. Releasing
   at either unready edge settles back immediately while the player
   prioritizes the missing photo. Swipe again once it is ready. Try both
   directions on a slow connection, including a drag back to an older
   cached preview while a new photo is downloading.
5. Record high-frame-rate video for touch-to-visible motion and pacing.
   Use the USB trace below to measure from the first moving touch report to
   the LCD DMA handoff, with cached neighbours and during background work.
   Use the video to measure physical touch to the first visible movement.
   The target is for 95% of cached gestures to move within 50 ms, with no
   visible tearing.
6. With Immich configured, wait for autoplay. Tap during the fade and check
   that it pauses and resumes smoothly. Grab it again, pull in each direction,
   and release after a short drag. The blended image should move with your
   finger until the gesture settles. Repeat while Immich is downloading or
   decoding a preview and check for visible tearing. With a two-second
   interval, verify each photo is fully visible for two seconds before fading.
7. Hold for three seconds with a photo visible and again during a transition.
   Check that the backlight turns off when you lift your finger. Wake the
   screen and drag without lifting. The photo should stay in place. Lift your
   finger and check that a new swipe works and autoplay waits the full interval.
   Save a lower brightness and check that it survives sleep, wake, and restart.
8. Set an on window that excludes the current local hour, hide the clock,
   and restart. After time sync, confirm the display turns off. Tap to wake,
   keep swiping through the timeout, and check that the screen turns off
   after the gesture settles. Confirm the local on hour wakes the display
   automatically and a previous manual sleep stays in effect until touch.
9. Set different daytime and nighttime brightness levels and select a timezone
   where the sun has already set. Hide the clock. After the frame syncs its
   time, confirm the nighttime brightness applies and a tap wakes the screen
   at that level. Restore the local timezone and confirm the display uses its
   corresponding level.
10. Rotate the frame physically and choose **Upside down (180°)**. After
    restarting, swipe forward and back, including over a portrait pair and
    a cold cache edge. Interrupt an automatic crossfade and check that the
    clock stays in the viewer's lower-left corner with no duplicate.

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
During the outage, use **Find frame on Wi-Fi** over USB and check it reports
that the frame is waiting. After reconnecting, check that it links to the
frame’s current address. Test with a syntactically valid but unusable Immich
configuration as well, so browser setup remains accessible before playback
works.

On a fresh native install, use the browser dialog to scan and join Wi-Fi
before entering any Immich settings. Restart and check that **Find frame
on Wi-Fi** reports the address. Complete Immich setup with the saved Wi-Fi
option. On a configured frame, also try an invalid Wi-Fi password in the
Improv dialog and check that the previous connection recovers.

On a configured frame, turn off its Wi-Fi network and verify that cached swipes
still work and the hotspot card appears after 90 seconds. Join the hotspot with
the password shown on the frame and open `http://192.168.4.1`. Try a wrong
password, then recover through a network on a different channel. Check that the
browser shows the assigned address, the hotspot closes after 15 seconds, and
Immich playback resumes with the existing settings after a reboot.

### Collect and compare performance traces

The Native Swipe Prototype CI installer enables USB performance traces.
For a local trace build, add `-D SWIPE_TRACE=ON` to the build command. A
normal local build leaves detailed traces disabled. Capture the 115200-baud
USB console from boot through the test and save each condition to a separate
file. Keep the log running until the tested slides finish preparing.

```sh
python3 firmware/native/tools/report_performance.py v1-warm-cache.log
```

Capture repeated swipes over warm cached slides, while fetching JPEG and WebP
previews, and with weak and disconnected Wi-Fi. Include steady slow drags,
quick flicks, reversals, and automatic crossfades. The report gives:

- First contact and first movement report to LCD handoff, plus the first
  moving frame's composition time. Contact-to-handoff includes the time
  until the finger moves far enough for the player to recognize a swipe.
- Changed-frame gaps by drag, settle or glide, and crossfade. Quiet and
  preparing samples show whether a slide preparation lease was active at
  both ends of the interval. A stationary finger or an easing animation
  can hold a frame intentionally. Read pacing with the video and the
  nominal panel period reported for the board.
- Ready-cache hits and misses at the first submitted movement, excluding
  the true edge of the library or retained history. The report also measures
  preparation from worker dispatch to cache publish and matches misses to
  the next ready event for the missing slide. It counts unresolved misses.
- Free and largest contiguous PSRAM blocks after display and cache
  allocation. Trace sequence gaps and logger overruns are flagged.

USB traces measure the firmware and DMA path. Repeat the camera test with
`SWIPE_TRACE=OFF` to check perceived latency and tearing without trace logging.

#### Score a high-speed swipe recording

Record the original real-time video at 120 fps or higher, preferably 240 fps.
Keep the point of finger contact and the whole panel visible. Start moving
as soon as your finger touches the panel for the latency test. Film cached
left and right swipes while the frame is quiet and while it is preparing
photos. Use separate clips for those conditions and record the board revision,
firmware build and playback settings with each clip. Keep the original file
with its high-speed timestamps. A slowed or interpolated export cannot
establish the real touch-to-motion time.

Step through the video in presentation order and number frames from zero.
If your player has no frame counter, extract numbered review frames with
FFmpeg. Keep using the original video for timing.

```sh
mkdir -p review
ffmpeg -i v1-warm.mov -map 0:V:0 -fps_mode passthrough -start_number 0 review/frame-%06d.png
```

Mark every attempted swipe in the test sequence, including swipes where the
photo never moved. Save the frame of first physical contact, the first frame
where the photo moves, and the last frame reviewed for that swipe. Leave
`motion_frame` empty if there was no visible response. Inspect the full
gesture for a split or discontinuity in the moving photo and mark tearing
`none`, `visible`, or `unclear`. Camera bands or ambiguous frames belong in
`unclear`. Include a frame before each contact, and finish reviewing each
swipe before the next one starts.

```csv
contact_frame,motion_frame,last_frame,tear
24,32,67,none
88,,121,unclear
```

With FFmpeg's `ffprobe` on `PATH`, run the report against the original clip.

```sh
python3 firmware/native/tools/report_optical_swipes.py v1-warm.mov v1-warm.csv
```

The report uses the actual frame timestamps and gives lower and upper bounds
for each response and for the measured P95. A camera frame can contain either
event anywhere between adjacent observations, so a bound spanning 50 ms leaves
that result unresolved. A swipe with no visible response keeps an unbounded
upper estimate. Review the latency coverage and tearing counts alongside the
board, workload and swipe count. Keep the video and CSV together so the
reported hashes can identify the evidence.
