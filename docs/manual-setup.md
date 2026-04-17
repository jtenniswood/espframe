---
title: Manual Setup
description: Install Espframe firmware via the ESPHome dashboard for full control over substitutions and configuration.
---

# Manual Setup

For advanced users: install via the ESPHome dashboard instead of the web installer to control substitutions and YAML.

## Create a configuration

New YAML in the ESPHome dashboard (example for Guition ESP32-P4 10"):

```yaml
substitutions:
  name: "immich-frame"
  friendly_name: "Espframe for Immich"

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

packages:
  espframe:
    url: https://github.com/jtenniswood/espframe
    files: [devices/guition-esp32-p4-jc8012p4a1/packages.yaml]
    ref: main
    refresh: 1s
```

Add `secrets.yaml` with `wifi_ssid` and `wifi_password`, then:

```bash
esphome run esphome.yaml
```

First build takes a few minutes; OTA updates are faster.

::: info ESPHome version
Current local builds use ESPHome `2026.4.0`. The shared configuration includes compatibility fixes for ESPHome 2026.3 and 2026.4 LVGL changes.
:::

## Substitutions

| Substitution | Default | Description |
|--------------|---------|-------------|
| `name` | — | Device name (required) |
| `friendly_name` | — | Web UI display name (required) |
| `immich_base_url` | *(empty)* | Pre-fill Immich URL to skip setup |
| `immich_api_key` | *(empty)* | Pre-fill API key to skip setup |
| `immich_slide_interval` | `2 minutes` | Slideshow interval |
| `immich_verify_ssl` | `false` | Set `true` to verify TLS |

## Pre-filling Immich credentials

To skip the first-boot wizard, add to substitutions:

```yaml
immich_base_url: "http://192.168.1.30:2283"
immich_api_key: !secret immich_api_key
```

Add `immich_api_key` to `secrets.yaml`. You can still change these later in the web UI.
