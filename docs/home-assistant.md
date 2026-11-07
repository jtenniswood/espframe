---
title: EspFrame Home Assistant Integration
description: Add EspFrame to Home Assistant as an ESPHome device for optional OTA updates, controls, and dashboard visibility.
---

# EspFrame Home Assistant Integration

Home Assistant is **not required** — the frame works standalone. If you run [Home Assistant](https://www.home-assistant.io/), you can add it as an ESPHome device for updates and dashboard control.

## Adding the Device

EspFrame runs on [ESPHome](https://esphome.io/), so Home Assistant often discovers it automatically.

- **If discovered:** **Settings → Devices & Services** → **ESPHome: 1 device discovered** → **Configure** → **Submit**
- **If not:** **Add Integration** → **ESPHome** → enter the device IP (on screen or web UI) → **Submit**

## API Encryption

EspFrame uses **runtime-provisioned per-device Noise** encryption for its ESPHome connection. Home Assistant generates a unique encryption key when it first connects, sends it to the frame, and then reconnects over the encrypted API. Existing plaintext integrations migrate on their first connection without changing the device or its entities.

Automatic key provisioning requires Home Assistant **2026.8.0** or newer. Home Assistant **2026.8.1** or newer is recommended because it also synchronizes the provisioned key with ESPHome Device Builder, preventing Device Builder from creating a competing key during adoption. Older Home Assistant versions can continue using ESPHome's temporary plaintext fallback until a key is provisioned.

**No shared or build-generated encryption key** is included in EspFrame firmware. No key is embedded in **public YAML**, **factory firmware**, or **release workflows**, and it is never exposed through the **device web UI** or **EspFrame backups**. The generated key is **Stored in device preferences and preserved across normal OTA updates**. It is separate from the Immich API key entered in the EspFrame web UI.

**A full erase or factory reinstall may require Home Assistant reconfiguration** because it can remove the stored encryption key. Normal firmware updates and rollbacks use OTA and retain it.

## Exposed Entities

Under **Settings → Devices & Services → ESPHome** (device page):

| Entity | Type | Description |
|--------|------|-------------|
| **Photos: Source** | Select | Deprecated preset adapter: All Photos, Favorites, Album, Person, Tag, Memories, or Custom — see [Smart Photo Filters](/photo-sources) |
| **Photos: Album IDs** | Text | Comma-separated Immich album UUIDs |
| **Photos: Album Labels** | Text | Optional friendly labels for saved album IDs |
| **Photos: Person IDs** | Text | Comma-separated Immich person UUIDs |
| **Photos: Person Labels** | Text | Optional friendly labels for saved person IDs |
| **Photos: Tag IDs** | Text | Comma-separated Immich tag UUIDs |
| **Photos: Tag Labels** | Text | Optional friendly labels for saved tag IDs |
| **Photos: Date Filter** | Switch | Turns photo date filtering on or off |
| **Photos: Date Filter Mode** | Select | Fixed Range or Relative Range |
| **Photos: Date From** | Text | Fixed range start date, in `YYYY-MM-DD` format |
| **Photos: Date To** | Text | Fixed range end date, in `YYYY-MM-DD` format |
| **Photos: Relative Amount** | Number | Rolling date range amount |
| **Photos: Relative Unit** | Select | Months or Years for the rolling date range |
| **Photos: Orientation** | Select | Any, Portrait Only, or Landscape Only |
| **Photos: Display Mode** | Select | Fill crops to cover the screen; Fit letterboxes without cropping |
| **Photos: Transition** | Select | Animation between photos: None, Cross Fade, Fade to Black/White, Slide Left/Right/Up/Down, or Random |
| **Photos: Transition Speed** | Select | Fast (0.5s), Normal (1s), or Slow (1.5s) |
| **Photos: Slideshow Interval** | Select | 10s–24h between photos |
| **Photos: Portrait Pairing** | Switch | Pair compatible portrait photos side-by-side |
| **Photos: Portrait Pairing Range** | Select | Match portraits from the same day, ±1 day, or ±2 days |
| **Photos: Paired Portraits Only** | Switch | Skip portrait photos unless a complete pair is ready |
| **Screen: Connection Timeout** | Select | 30s–30min before showing connection-failed screen |
| **Screen: Rotation** | Select | LVGL screen rotation: 0 or 180 degrees |
| **Screen: Backlight** | Light | On/off and brightness (0–100%). Turning it off puts the frame to sleep; turning it on wakes manual sleep unless scheduled off-hours are active. |
| **Screen: Sleep** | Button | Puts the display into the same sleep state as the touchscreen hold gesture, pausing slideshow fetches. |
| **Screen: Wake** | Button | Wakes the display and resumes the slideshow. During scheduled off-hours, this is a temporary wake using the configured wake timeout. |
| **Firmware: Auto Update** | Switch | Install updates when available |
| **Firmware: Update Frequency** | Select | Hourly, Daily, Weekly, Monthly |
| **Firmware: Check for Update** | Button | Manual stable update check |
| **Firmware: Version** | Text Sensor | Installed version |
| **ESP32-C6: Update Available** | Text Sensor | Wi-Fi coprocessor update status |
| **ESP32-C6: Current Firmware** | Text Sensor | Installed Wi-Fi coprocessor firmware version |
| **ESP32-C6: Available Firmware** | Text Sensor | Latest compatible Wi-Fi coprocessor firmware version |
| **Firmware ESP32-C6: Check for Update** | Button | Manual Wi-Fi coprocessor update check |
| **Firmware ESP32-C6: Install Update** | Button | Install the available Wi-Fi coprocessor firmware update |
| **Network: Online** | Binary Sensor | Connection status |
| **Network: WiFi Strength** | Sensor | Signal % |
| **Network: IP Address** | Text Sensor | Device IP |
| **Reset Reason** | Text Sensor | Last reboot reason |

## Automations

Use entities in automations, e.g.: press **Screen: Sleep** when a room becomes empty and **Screen: Wake** when it becomes occupied; change **Photos: Slideshow Interval**, **Photos: Source**, or the date filter by time; notify when **Network: Online** goes unavailable; trigger **Firmware: Check for Update** from a script.

## Camera Notifications

Home Assistant can pop a camera snapshot onto the frame when a doorbell rings or motion is detected. The frame wakes if it is asleep, pauses the slideshow, shows the snapshot full screen with an optional title, and goes back to what it was doing when the time runs out. Tap the screen to close it early; a tap keeps the screen awake like any other touch.

The frame adds these ESPHome actions, named after the device (for example `esphome.immich_frame_10inch_abc123_show_camera`):

| Action | Fields | Description |
|--------|--------|-------------|
| **show_camera** | `url`, `title`, `duration`, `refresh`, `fill` | Show a JPEG or WebP snapshot from any HTTP(S) URL |
| **show_camera_with_header** | the above, plus `header_name`, `header_value` | Same, with one request header for sources that need authentication |
| **dismiss_camera** | — | Close the overlay and restore the previous sleep state |

- `duration`: seconds on screen. `0` uses 30 seconds; the maximum is 3600.
- `refresh`: seconds between new snapshots for a near-live view. `0` shows a single snapshot.
- `fill`: `true` crops the snapshot to cover the screen; `false` shows the whole image with black bars where the shapes differ.
- `title`: shown across the top. Leave it empty to show only the image.

Calling `show_camera` again while the overlay is open swaps the image and restarts the timer.

### Any Home Assistant camera

Every camera Home Assistant knows about (UniFi Protect, Reolink, Frigate, ONVIF, and others) can be served through Home Assistant's camera proxy. The camera's `access_token` attribute is a short-lived token for that proxy, so no extra credentials are needed:

```yaml
automation:
  - alias: Doorbell on EspFrame
    triggers:
      - trigger: state
        entity_id: event.front_door_doorbell
    actions:
      - action: esphome.immich_frame_10inch_abc123_show_camera
        data:
          url: "http://homeassistant.local:8123/api/camera_proxy/camera.front_door?token={{ state_attr('camera.front_door', 'access_token') }}&width=1280"
          title: "Someone is at the door"
          duration: 30
          refresh: 2
          fill: true
```

That token rotates every few minutes. For long overlays, use a [long-lived access token](https://www.home-assistant.io/docs/authentication/#your-account-profile) instead:

```yaml
      - action: esphome.immich_frame_10inch_abc123_show_camera_with_header
        data:
          url: "http://homeassistant.local:8123/api/camera_proxy/camera.front_door?width=1280"
          title: "Motion in the driveway"
          duration: 120
          refresh: 2
          fill: true
          header_name: "Authorization"
          header_value: "Bearer YOUR_LONG_LIVED_TOKEN"
```

### Direct camera snapshots

The frame can also fetch straight from the camera or NVR, without going through Home Assistant:

- **UniFi Protect:** turn on **Anonymous Snapshot** in the camera's settings, then use `http://<camera-ip>/snap.jpeg`.
- **Frigate:** `http://<frigate-host>:5000/api/<camera>/latest.jpg?h=800`
- **Other cameras:** any URL that returns a JPEG or WebP image works. Use `show_camera_with_header` if it needs an API key or `Authorization` header.

Larger sources take longer to download and decode, so ask the source for a smaller image (`width=1280`, `h=800`) when it supports that. Each refresh opens a new connection, and an HTTPS handshake can take about a second, so for refresh intervals of a few seconds prefer a local `http://` address. Live video streams (RTSP, MJPEG) are not supported; `refresh` re-fetches snapshots instead.
