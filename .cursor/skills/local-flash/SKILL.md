---
name: local-flash
description: Compile and flash ESPHome firmware to a USB-connected ESP32 device using the local dev build config. Use when the user says "flash", "local flash", "build and flash", "upload firmware", "flash over USB", or wants to test firmware changes on a physical device.
---

# Local Flash via USB

Compile ESPHome firmware with locally bundled web UI files, then flash to a USB-connected device using esptool.

## Prerequisites

- `guition-esp32-p4-jc8012p4a1/dev.yaml` must exist (creates a build with local CSS/JS bundled in)
- `guition-esp32-p4-jc8012p4a1/secrets.yaml` must exist with `wifi_ssid` and `wifi_password`
- Docker with `ghcr.io/esphome/esphome:latest` image (the local pip esphome is too old for ESP32-P4)
- `esptool.py` or `esptool` available on PATH
- Device connected via USB

## Workflow

### 1. Compile with Docker

```bash
docker run --rm \
  -v /Users/jtenniswood/Library/CloudStorage/Dropbox/Git/espframe:/config \
  ghcr.io/esphome/esphome:latest \
  compile guition-esp32-p4-jc8012p4a1/dev.yaml
```

This bakes the local `docs/public/webserver/app.js` and `docs/public/webserver/style.css` into the firmware. Takes ~5-9 minutes on first build, faster on subsequent builds due to cached build artifacts.

### 2. Find the USB serial port

```bash
ls /dev/tty.usb*
```

Typical result: `/dev/tty.usbserial-201230`

### 3. Flash with esptool

```bash
esptool.py --port /dev/tty.usbserial-XXXXXX --chip esp32p4 write_flash 0x0 \
  guition-esp32-p4-jc8012p4a1/.esphome/build/immich-frame-10inch/.pioenvs/immich-frame-10inch/firmware.factory.bin
```

Replace `XXXXXX` with the actual serial port suffix from step 2.

**Faster flashing:** Add `-b 2000000` (or `-b 921600` if 2M is unstable) to raise serial baud rate. Default is 460800; higher baud = fewer seconds. Example:

```bash
esptool.py --port /dev/tty.usbserial-XXXXXX --chip esp32p4 -b 2000000 write_flash 0x0 \
  guition-esp32-p4-jc8012p4a1/.esphome/build/immich-frame-10inch/.pioenvs/immich-frame-10inch/firmware.factory.bin
```

If you see random write errors, retry with a lower baud (e.g. `-b 921600` or omit for default).

### 4. Verify

The device resets automatically after flashing. Wait 15-20 seconds for boot + WiFi connection, then open `http://<device-ip>/` in a browser.

## Speed up the full process

- **Flash only (no compile):** If you didn't change YAML or web UI, skip step 1 and run step 3 against the existing `firmware.factory.bin`. Saves ~1–6 minutes.
- **Faster serial:** Use `-b 2000000` (or `-b 921600`) in the esptool command (see step 3). Can cut flash time from ~90s to ~25–45s depending on cable/adapter.
- **Compile time:** First compile is slow; later builds use cache. Keep the repo (and Docker volume) so `.esphome/build` is reused.

## Troubleshooting

- **"No USB serial devices found"**: Check the USB cable supports data (not power-only) and the device is powered on.
- **Docker compile fails with ESP32P4 unknown**: Pull the latest image with `docker pull ghcr.io/esphome/esphome:latest`.
- **esptool connection timeout**: Hold the BOOT button on the device while running the flash command, release after "Connecting..." appears.
- **Docker can't flash directly**: On macOS, Docker runs in a VM and cannot access host USB devices. Always use `esptool` on the host for flashing.
- **"Could not find __init__.py for component online_image"**: In `dev.yaml`, the local component name is `remote_image`, not `online_image`. Use `components: [gsl3680, remote_image, espframe]` in `external_components`.
