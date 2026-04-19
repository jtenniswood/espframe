# MicroLink v2 Architecture

## Task Model

MicroLink v2 runs 4 independent FreeRTOS tasks communicating via queues:

| Task | Stack | Purpose |
|---|---|---|
| `coord_task` | 12KB | Tailscale control plane (Noise handshake, HTTP/2, registration, long-poll) |
| `derp_tx_task` | 14KB | DERP relay TX queue (TLS writes to relay server) |
| `net_io_task` | 8KB | Unified select() loop (DISCO UDP, STUN UDP, WG packet routing) |
| `wg_mgr_task` | 8KB | WireGuard handshakes, DISCO ping/pong, peer management |

No task shares mutexes with another. All communication is via FreeRTOS queues and event groups.

## Data Path

### WiFi Mode
```
App UDP send → WireGuard encrypt → lwIP → WiFi → Direct UDP (or DERP relay)
```

### Cellular Mode (AT Socket Bridge)
```
App UDP send → WireGuard encrypt → DERP TX queue
    → ml_derp.c TLS write → ml_at_socket.c AT+CIPSEND
    → UART → SIM7600/SIM7670 internal TCP/IP → 4G network → DERP server
```

All cellular traffic goes through DERP relay because carrier-grade NAT (CGNAT) blocks inbound UDP.

## Network Switching (ml_net_switch)

Full stop/start cycle — no hot-swap:

```
IDLE → WIFI_CONNECTING → WIFI_VPN_UP → [health fail x3] → SWITCHING_TO_CELL
    → CELL_CONNECTING → CELL_VPN_UP → [failback timer] → SWITCHING_TO_WIFI
    → WIFI_CONNECTING → WIFI_VPN_UP (or back to CELL_VPN_UP if WiFi still down)
```

Health check: periodic timer checks `microlink_is_connected()`. After 3 consecutive failures, triggers switch.

## Cellular DERP Optimizations

10 reliability fixes for cellular DERP relay operation:

1. **force_derp_output**: All WG output goes through DERP callback (CGNAT blocks direct UDP)
2. **TX queue priority**: WG handshake packets use `xQueueSendToFront()` to bypass DISCO queue
3. **DISCO suppression**: No proactive DISCO probes on cellular (direct paths impossible)
4. **CallMeMaybe suppression**: CGNAT endpoints are unreachable, skip CMM
5. **Handshake rate-limit**: Max 1 WG handshake init per 10s (prevents 5Hz heartbeat spam)
6. **DERP connect retry**: 3 attempts with 2s backoff
7. **DERP reconnect handler**: Auto-reconnect on write failure
8. **Read timeout**: 200ms after DERP handshake (was 10s from connect phase)
9. **32 DERP regions**: Raised from 16 (Tailscale now serves 28 regions)
10. **DERP host fallback**: derp9e.tailscale.com

## Memory Layout

| Region | Usage |
|---|---|
| SRAM (~320KB) | Tasks, queues, event groups, WG crypto state |
| PSRAM (8MB) | Coord buffer (512KB), peer list, NVS blob cache |

## Module Files

| File | Purpose |
|---|---|
| `microlink.c` | Public API, init/start/stop/destroy |
| `ml_coord.c` | Tailscale control plane (Noise IK, HTTP/2 long-poll) |
| `ml_derp.c` | DERP relay client + TX task |
| `ml_net_io.c` | Unified network I/O select loop |
| `ml_wg_mgr.c` | WireGuard + DISCO + peer management |
| `ml_stun.c` | Async STUN (Tailscale primary + Google fallback) |
| `ml_udp.c` | UDP socket API for applications |
| `ml_cellular.c` | SIM7600/SIM7670 modem driver |
| `ml_at_socket.c` | AT command socket bridge (modem internal TCP/IP) |
| `ml_net_switch.c` | WiFi/cellular failover state machine |
| `ml_peer_nvs.c` | NVS peer cache (blob-based, LRU eviction) |
| `ml_noise.c` | Noise IK protocol |
| `ml_h2.c` | HTTP/2 framing |
| `ml_zerocopy.c` | Zero-copy WG via raw lwIP PCB |
