# Getting Started

::: tip Prefer a simpler setup?
You can flash directly from your browser with the [Web Installer](./install) — no ESPHome toolchain needed.
:::

This guide covers the manual ESPHome install for users who want to customize substitutions or compile from source.

## What You Need

1. **A [Guition ESP32-P4 (JC8012P4A1)](https://www.guition.com/)** — 10" 1280x800 touchscreen display
2. **An [Immich](https://immich.app/) server** running on your local network
3. **An Immich API key** — generate one in your Immich web UI under *Account Settings > API Keys*
4. **[ESPHome](https://esphome.io/) installed** — via Home Assistant add-on, Docker, or `pip install esphome`

## Step 1: Create Your Config

Create a new folder for your project and add two files:

**`esphome.yaml`**

```yaml
substitutions:
  name: "immich-frame"
  friendly_name: "Immich Frame"

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

packages:
  espframe:
    url: https://github.com/jtenniswood/espframe
    files: [guition-esp32-p4-jc8012p4a1/packages.yaml]
    ref: main
    refresh: 1s
```

**`secrets.yaml`** (same folder)

```yaml
wifi_ssid: "YourWiFiName"
wifi_password: "YourWiFiPassword"
```

::: tip
You can optionally set `immich_base_url` and `immich_api_key` as substitutions to bake them into the firmware, but it's easier to configure them on-screen after first boot.
:::

## Step 2: Flash the Firmware

Connect the board via USB and run:

```bash
esphome run esphome.yaml
```

ESPHome will download the remote packages, compile, and flash the firmware. This first build takes a few minutes.

## Step 3: First Boot

Once flashed, the board walks you through setup on-screen:

### 1. Loading screen

A progress bar appears while the device starts up.

### 2. WiFi connection

If your WiFi credentials are in `secrets.yaml`, the device connects automatically. If not, it creates a hotspot — connect to it with your phone and follow the captive portal to enter your WiFi details.

### 3. Immich setup screen

Once connected to WiFi, the screen shows a URL like `http://192.168.x.x`. Open that address in your browser to see the device's web UI. Enter your **Immich URL** (e.g. `http://192.168.1.30:2283`) and **API key**, then hit save.

### 4. Photos start showing

The slideshow begins automatically. Your Immich photos will start appearing on screen.

## Next Steps

- **[Usage](./usage)** — learn the touch gestures and what's shown on screen
- **[Configuration](./configuration)** — adjust slideshow speed, clock, timezone, and more
- **[Troubleshooting](./troubleshooting)** — fixes for common issues
