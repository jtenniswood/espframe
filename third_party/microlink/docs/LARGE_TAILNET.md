# Large Tailnet Considerations

MicroLink v2 has been tested with 1000+ peer tailnets. This document covers configuration and limits.

## Configuration

### ML_MAX_PEERS (Kconfig)

Controls max simultaneous WireGuard peers with active tunnels.

```
idf.py menuconfig → MicroLink V2 → Maximum simultaneous WireGuard peers
```

| Setting | Memory | Use case |
|---|---|---|
| 1-4 | ~800B | Most deployments (only talk to 1-4 peers) |
| 16 | ~3.2KB | Default, small-medium tailnets |
| 64 | ~12.8KB | Large tailnets with many active peers |

**Important**: This is NOT the total tailnet size — it's how many peers have active WG tunnels simultaneously. A 1000-peer tailnet typically only needs `max_peers=4` if only communicating with a few peers.

### ML_NVS_MAX_PEERS (Kconfig)

Controls NVS flash cache for peer metadata (persists across reboots).

```
idf.py menuconfig → MicroLink V2 → Maximum cached peers in NVS
```

| Setting | NVS Blob Size | Partition Requirement |
|---|---|---|
| 16 | ~1.5KB | Default NVS partition |
| 64 | ~5.9KB | Default NVS partition |
| 256 | ~23.6KB | Default NVS partition |
| 1024 | ~94.2KB | Custom NVS partition (≥128KB) |

For 1024 peers, you need a custom partition table with a larger NVS partition:
```csv
# Name,   Type, SubType, Offset,  Size,    Flags
nvs,      data, nvs,     ,        0x20000,
phy_init, data, phy,     ,        0x1000,
factory,  app,  factory, ,        0x300000,
```

### Coordination Buffer

Large peer lists require larger HTTP/2 response buffers:

```
idf.py menuconfig → MicroLink V2 → Coord buffer size (KB)
```

- Default: 64KB (sufficient for ~200 peers)
- Large tailnets: 512KB (PSRAM-backed)

Already configured in `microlink_internal.h`:
- H2 WINDOW_UPDATE: proactive updates for responses >64KB
- Recv timeout: 60s (control plane may be slow with large peer lists)

## DISCO Scaling

With 100+ peers, DISCO probe intervals are staggered to prevent network flooding:

- Peers are probed in batches of 10
- 3s delay between batches
- On cellular: all proactive DISCO is suppressed (only respond to incoming probes)

## Delta Updates

MicroLink v2 supports Tailscale's incremental peer updates:
- `PeersChanged`: Only changed peers sent
- `PeersRemoved`: Only removed peer IDs sent
- `PeersChangedPatch`: Minimal field-level patches

This means a 1000-peer tailnet doesn't re-download the full peer list on every long-poll response.

## Recommendations

1. **Set `max_peers` to actual need** — not the tailnet size. If your ESP32 only talks to 1 server, set `max_peers=4`.
2. **Use PSRAM** — Required for large tailnets. All ESP32-S3 boards we support have 8MB PSRAM.
3. **Use reusable auth keys** — Large deployments should use pre-generated reusable keys.
4. **IMEI-based naming** — For cellular deployments, `microlink_imei_device_name()` generates unique names.
5. **NVS partition** — For 1024+ cached peers, use a custom partition table.
