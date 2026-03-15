---
title: Manual Setup
description: Install Espframe firmware via the ESPHome dashboard for full control over substitutions and configuration.
---

# Manual Setup

Install the espframe firmware via the ESPHome dashboard instead of the web installer. This gives you full control over substitutions and lets you customise behaviour that the web installer leaves at defaults.

## Prerequisites

- **ESPHome** running (as a [Home Assistant add-on](https://esphome.io/guides/getting_started_hassio.html) or [standalone](https://esphome.io/guides/installing_esphome.html))
- Your device connected via **USB** for the first flash (OTA updates work after that)
- An **Immich server** running on your network — [immich.app](https://immich.app/)
- An **Immich API key** — see [Creating an API Key](./api-key) for which permissions to select

## Create a configuration

In the ESPHome dashboard, create a new YAML configuration for your device. Use the example below as a starting point.

### Guition ESP32-P4 (10")

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

Add a `secrets.yaml` file in the same folder:

```yaml
wifi_ssid: "YourWiFiName"
wifi_password: "YourWiFiPassword"
```

Then flash via USB:

```bash
esphome run esphome.yaml
```

ESPHome will download the remote packages, compile, and flash the firmware. The first build takes a few minutes; subsequent OTA updates are much faster.

## Available substitutions

These substitutions can be added to the `substitutions:` block in your configuration to override the defaults.

| Substitution | Default | Description |
|---|---|---|
| `name` | — | Device name used on your network (required) |
| `friendly_name` | — | Display name shown in the web UI (required) |
| `immich_base_url` | *(empty)* | Pre-fill the Immich server URL to skip the setup screen on first boot |
| `immich_api_key` | *(empty)* | Pre-fill the API key to skip the setup screen on first boot |
| `immich_slide_interval_seconds` | `15` | Default slideshow interval in seconds |
| `immich_verify_ssl` | `false` | Set to `true` to enforce TLS certificate verification |

::: tip
You can optionally bake in `immich_base_url` and `immich_api_key` so the device skips the on-screen setup wizard. If you leave them empty, the device will prompt you to enter them via the built-in web UI on first boot.
:::

## Pre-filling Immich credentials

To skip the first-boot setup screen entirely, add the Immich connection details to your substitutions:

```yaml
substitutions:
  name: "immich-frame"
  friendly_name: "Immich Frame"
  immich_base_url: "http://192.168.1.30:2283"
  immich_api_key: !secret immich_api_key
```

Add the API key to your `secrets.yaml`:

```yaml
wifi_ssid: "YourWiFiName"
wifi_password: "YourWiFiPassword"
immich_api_key: "your-immich-api-key-here"
```

::: tip
Even with credentials baked in, you can change them at any time via the [web UI](./configuration#immich-connection) — no reflash required.
:::
