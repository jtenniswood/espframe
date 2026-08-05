---
title: ESPHome Manual Setup for Espframe
description: Install Espframe firmware from the ESPHome dashboard when you want full control over YAML substitutions and local builds.
---

# ESPHome Manual Setup for Espframe

For advanced users: install via the ESPHome dashboard instead of the web installer to control substitutions and YAML.

## Create a configuration

First choose the package matching the four-digit number printed on the rear case:

| Rear-case marking | Panel profile | Package file |
|---|---|---|
| `2627` or lower | Original panel | `devices/guition-esp32-p4-jc8012p4a1/packages.yaml` |
| `2628` or higher | New panel | `devices/guition-esp32-p4-jc8012p4a1-v2/packages.yaml` |

The newer panel may not say `V2`; use the rear-case number rather than the visible model name. New YAML in the ESPHome dashboard for the original panel:

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

For the new panel, use the matching package instead:

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
    files: [devices/guition-esp32-p4-jc8012p4a1-v2/packages.yaml]
    ref: main
    refresh: 1s
```

Add `secrets.yaml` with `wifi_ssid` and `wifi_password`, then:

```bash
esphome run esphome.yaml
```

First build takes a few minutes; OTA updates are faster.

::: info ESPHome version
Current local builds use ESPHome `2026.7.4`. The shared configuration includes compatibility fixes for ESPHome 2026.3, 2026.4, and 2026.7 LVGL changes.
:::

## Substitutions

| Substitution | Default | Description |
|--------------|---------|-------------|
| `name` | — | Device name (required) |
| `friendly_name` | — | Web UI display name (required) |
| `immich_base_url` | *(empty)* | Pre-fill Immich URL to skip setup |
| `immich_api_key` | *(empty)* | Pre-fill API key to skip setup |
| `immich_slide_interval` | `15 seconds` | Slideshow interval |
| `immich_verify_ssl` | `false` | Set `true` to verify TLS certificates |
| `ntp_server_1` | `0.pool.ntp.org` | First NTP server used for clock sync |
| `ntp_server_2` | `1.pool.ntp.org` | Second NTP server used for clock sync |
| `ntp_server_3` | `2.pool.ntp.org` | Third NTP server used for clock sync |

## Pre-filling Immich credentials

To skip the first-boot wizard, provide a valid Immich URL and API key in substitutions:

```yaml
immich_base_url: "https://photos.example.com"
immich_api_key: !secret immich_api_key
```

Add `immich_api_key` to `secrets.yaml`. The URL can also be a direct local address such as `http://192.168.1.30:2283`. You can still change these later in the web UI.

## Custom NTP servers

You can change NTP servers later in the Espframe web settings under **Clock**. To pre-fill them before flashing, add substitutions:

```yaml
ntp_server_1: "172.20.32.1"
ntp_server_2: "172.20.41.1"
ntp_server_3: "172.20.32.1"
```
