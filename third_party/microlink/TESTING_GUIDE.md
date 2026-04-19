# MicroLink Testing Guide

This guide covers hardware setup, building, flashing, and testing MicroLink examples.

## Prerequisites

- **ESP-IDF v5.3+** installed ([setup guide](https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32s3/get-started/))
- **Tailscale account** with an auth key from [admin.tailscale.com](https://login.tailscale.com/admin/settings/keys)
  - Generate a **reusable + ephemeral** key for development
- **USB-C cable** for flashing

## Hardware Options

### Option A: XIAO ESP32S3 + Waveshare SIM7600X (Separate Boards)

| Connection | Pin |
|---|---|
| ESP32 TX → Modem RXD | GPIO43 (XIAO D6) |
| ESP32 RX ← Modem TXD | GPIO44 (XIAO D7) |
| GND | Connected |
| Power | Separate USB-C for each board |

### Option B: LILYGO T-SIM7670G-S3 (All-in-One)

| Connection | Pin |
|---|---|
| ESP32 TX → Modem RXD | GPIO4 |
| ESP32 RX ← Modem TXD | GPIO5 |
| PWRKEY | GPIO46 |
| DTR | GPIO7 |
| Power | Single USB-C |

Use the `lilygo_sim7670g` example — pin configuration is preset in sdkconfig.defaults.

## Setup Credentials

**All credentials are configured via `idf.py menuconfig`, not in source code.**

1. Copy the credential template:
   ```bash
   cd examples/<example_name>
   cp sdkconfig.credentials.example sdkconfig.credentials
   ```

2. Edit `sdkconfig.credentials` with your values:
   ```
   CONFIG_ML_WIFI_SSID="YourWiFi"
   CONFIG_ML_WIFI_PASSWORD="YourPassword"
   CONFIG_ML_TAILSCALE_AUTH_KEY="tskey-auth-..."
   CONFIG_ML_CELLULAR_SIM_PIN="0000"
   CONFIG_ML_CELLULAR_APN="your-apn"
   ```

3. Or set interactively: `idf.py menuconfig` → MicroLink V2 → Credentials

## Build & Flash

```bash
# Source ESP-IDF
source ~/esp/esp-idf/export.sh

# Build
cd examples/<example_name>
idf.py build

# Flash + monitor
idf.py -p /dev/ttyACM0 flash monitor
```

## Test 1: WiFi Only (basic_connect)

Tests WiFi → Tailscale VPN → bidirectional UDP.

```bash
cd examples/basic_connect
idf.py build && idf.py -p /dev/ttyACM0 flash monitor
```

**Verify on your PC (same Tailscale network):**
```bash
# Check ESP32 appears on tailnet
tailscale status | grep esp32

# Ping the ESP32's VPN IP
tailscale ping <ESP32_VPN_IP>

# ICMP ping
ping <ESP32_VPN_IP>

# UDP echo test
echo "ping" | nc -u <ESP32_VPN_IP> 9000
# Expected: receives "pong"
```

**Expected serial output:**
```
WiFi connected, IP: 192.168.x.x
VPN connected: 100.x.x.x
UDP socket ready: 100.x.x.x:9000
```

## Test 2: Cellular Only (cellular_connect)

Tests 4G cellular modem → PPP data connection → Tailscale VPN → bidirectional UDP.
Supports PPP (preferred) with automatic fallback to AT socket bridge if PPP fails.

```bash
cd examples/cellular_connect
idf.py build && idf.py -p /dev/ttyACM0 flash monitor
```

**Verify bidirectional UDP:**
```bash
# Check ESP32 appears on tailnet
tailscale status | grep esp32

# DISCO layer test
tailscale ping <ESP32_VPN_IP>

# ICMP through WireGuard tunnel
ping -c 5 <ESP32_VPN_IP>

# Bidirectional UDP — send a message and listen for responses
nc -u <ESP32_VPN_IP> 9000
# Type: ping, stats, echo:<message>
```

**Expected serial output:**
```
SIM ready, IMEI: XXXXXXXXXXX
Registered on network: <carrier>
PPP connected — using lwIP standard sockets
DERP connect: derp9e.tailscale.com
VPN connected: 100.x.x.x
```

**Performance characteristics:**

| Transport | UDP RTT | ICMP RTT | Boot → Connected |
|-----------|---------|----------|------------------|
| PPP (direct UDP) | 300-600ms | 400-700ms | ~35-50s |
| AT socket bridge (DERP relay) | 3-15s | 5-15s | ~60-90s |

## Test 3: WiFi/Cellular Failover (failover_connect)

Tests automatic switching between WiFi and cellular.

```bash
cd examples/failover_connect
idf.py build && idf.py -p /dev/ttyACM0 flash monitor
```

**Failover test procedure:**
1. Start with WiFi available → connects via WiFi
2. Turn off WiFi router → after ~90s, switches to cellular
3. Turn WiFi back on → after ~120s, fails back to WiFi

**Expected serial output during failover:**
```
=== CONNECTED via WiFi ===
[WiFi] state=WIFI_VPN_UP tx=5 rx=3
Health check failed (1/3)
Health check failed (2/3)
Health check failed (3/3)
=== SWITCHING: WiFi -> Cellular ===
Starting cellular modem...
Cellular modem connected
=== CONNECTED via Cellular ===
[Cell] state=CELL_VPN_UP tx=12 rx=8
Failback check: attempting WiFi reconnection...
=== SWITCHING: Cellular -> WiFi ===
=== CONNECTED via WiFi ===
```

## Troubleshooting

### WiFi Issues
- **"WiFi connection failed"**: Check SSID/password in menuconfig
- **"VPN connection timeout"**: Check Tailscale auth key is valid and not expired

### Cellular Issues
- **"AT communication failed"**: Check UART wiring (TX↔RX crossed correctly)
- **"SIM PIN rejected"**: Verify PIN code
- **"Network registration timeout"**: Ensure SIM has active data plan, check antenna
- **"DERP connect failed"**: Modem has IP but DNS/TLS failing — check APN settings
- **"PPP CHAP authentication failed"**: Check APN/username/password for your carrier

### General
- **Build error "ML_CELLULAR_PWRKEY_PIN undeclared"**: Run `idf.py fullclean` then `idf.py build`
- **Flash fails**: Hold BOOT button on XIAO while pressing reset, then flash
- **No serial output**: Check USB-C cable is data-capable (not charge-only)
