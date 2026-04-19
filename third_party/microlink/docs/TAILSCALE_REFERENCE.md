# Tailscale Native Client Reference

Key architecture patterns from tailscaled that MicroLink v2 should replicate.
Reference source: `../../tailscale-reference/tailscale/`

## Reference File Map

| MicroLink v2 Module | Tailscale Go Reference |
|---------------------|----------------------|
| ml_coord.c | `control/controlclient/auto.go`, `control/controlclient/direct.go`, `control/controlclient/map.go` |
| ml_derp.c | `derp/derp_client.go`, `derp/derphttp/derphttp_client.go`, `wgengine/magicsock/derp.go` |
| ml_net_io.c | `wgengine/magicsock/magicsock.go` (receiveIPv4, receiveIPv6, receiveDERP) |
| ml_wg_mgr.c | `wgengine/magicsock/endpoint.go`, `wgengine/magicsock/magicsock.go` |
| ml_stun.c | `net/netcheck/netcheck.go` |
| ml_noise.c | `control/controlbase/noise.go` |
| ml_h2.c | N/A (tailscale uses Go's net/http2, we do manual framing) |

## Critical Timing Constants (from tailscaled source)

These MUST be replicated exactly for correct protocol behavior:

```c
// DISCO timing (from wgengine/magicsock/endpoint.go)
#define DISCO_PING_INTERVAL_MS          5000    // Min between pings to same endpoint
#define DISCO_HEARTBEAT_MS              3000    // Heartbeat to active peers
#define DISCO_TRUST_DURATION_MS         6500    // Trust confirmed UDP path
#define DISCO_PING_TIMEOUT_MS           5000    // Pong wait timeout
#define DISCO_UPGRADE_INTERVAL_MS       60000   // Path upgrade attempt interval
#define DISCO_SESSION_ACTIVE_TIMEOUT_MS 45000   // Stop keepalives for idle peers

// STUN timing (from net/netcheck/netcheck.go)
#define STUN_RETRANSMIT_MS              100     // Between retransmissions
#define STUN_TOTAL_TIMEOUT_MS           5000    // Total probe timeout
#define STUN_RESTUN_INTERVAL_MS         23000   // Periodic re-STUN (20-26s randomized)

// DERP timing (from derp/derp_client.go, wgengine/magicsock/derp.go)
#define DERP_KEEPALIVE_S                60      // Server sends keepalive
#define DERP_RECV_TIMEOUT_S             120     // Client read timeout (2x keepalive)
#define DERP_CONNECT_TIMEOUT_MS         10000   // HTTP upgrade timeout
#define DERP_WRITE_QUEUE_DEPTH          32      // Per-region write channel depth
#define DERP_INACTIVE_CLEANUP_S         60      // Close idle non-home DERP
#define DERP_READER_BACKOFF_MAX_S       5       // Max backoff on read errors

// Control plane timing (from control/controlclient/auto.go)
#define CTRL_WATCHDOG_S                 120     // Long-poll watchdog
#define CTRL_BACKOFF_MAX_S              30      // Max reconnect backoff
#define CTRL_KEEPALIVE_S                60      // Server keepalive interval

// WireGuard
#define WG_HANDSHAKE_TIMEOUT_S          5       // Handshake attempt timeout
#define WG_REKEY_INTERVAL_S             120     // Rekey every 2 minutes
```

## DERP Architecture Pattern

### Go source: `wgengine/magicsock/derp.go`

Per-region DERP connection spawns 3 goroutines:

```go
// runDerpReader - sole owner of dc.Recv()
func (c *Conn) runDerpReader(ctx context.Context, derpFakeAddr netip.AddrPort, dc *derphttp.Client, ...) {
    for {
        msg, connGen, err := dc.RecvDetail()
        // ... classify and dispatch
    }
}

// runDerpWriter - sole owner of dc.Send()
func (c *Conn) runDerpWriter(ctx context.Context, dc *derphttp.Client, ch <-chan derpWriteRequest, ...) {
    for wr := range ch {
        dc.Send(wr.pubKey, wr.b)
    }
}
```

**Key insight:** The writer goroutine blocks on channel receive, NOT on TLS write.
The channel (`writeCh`, depth 32) provides natural backpressure. When full, the
sender (not the writer) handles dropping.

### Backpressure strategy (from `wgengine/magicsock/derp.go`):

```go
func (c *Conn) sendDerpTo(regionID int, pubKey key.NodePublic, b []byte) error {
    ch := c.derpWriteChanForRegion(regionID)
    if ch == nil { return errDERPNotAvail }

    // Try to send. If full, drop oldest and retry (up to 3 times).
    select {
    case ch <- wr:
        return nil
    default:
    }
    for i := 0; i < 3; i++ {
        select {
        case <-ch:    // Drop oldest
        default:
        }
        select {
        case ch <- wr:
            return nil
        default:
        }
    }
    return errDropDerpPacket
}
```

**ESP32 equivalent:** `xQueueSend()` with 0 timeout. On failure, `xQueueReceive()`
to dequeue oldest, then retry `xQueueSend()`.

## Control Plane Pattern

### Go source: `control/controlclient/auto.go`

Three independent goroutines, each with its own backoff:

```go
func (c *Auto) Start() {
    go c.authRoutine()
    go c.mapRoutine()
    go c.updateRoutine()
}
```

**mapRoutine** maintains the streaming long-poll:
- Sends MapRequest with `Stream=true`
- Reads frames in a loop (4-byte length prefix + zstd JSON)
- 120-second watchdog: no data = reconnect
- Keep-alive frames reset watchdog but skip processing

**updateRoutine** sends local state changes:
- Endpoint updates, hostinfo changes
- Uses separate mechanism from the long-poll stream
- Never sends Stream=false on the long-poll connection

**ESP32 mapping:** Single `coord` task handles all three roles sequentially
(we don't need 3 tasks since ESP32 has limited resources). But the principle
holds: endpoint updates use a DIFFERENT HTTP/2 stream from the long-poll.

## DISCO Rate Limiting Pattern

### Go source: `wgengine/magicsock/endpoint.go`

```go
func (de *endpoint) heartbeat() {
    if de.lastSend.Load().Before(now.Add(-sessionActiveTimeout)) {
        return // Peer inactive, stop heartbeats
    }
    de.mu.Lock()
    defer de.mu.Unlock()
    if de.heartBeatTimer == nil {
        return // Shutdown
    }
    de.heartBeatTimer.Reset(heartbeatInterval)  // 3 seconds
    de.startDiscoPingLocked(discoPingHeartbeat, 0, de.bestAddr.AddrPort)
}
```

```go
func (de *endpoint) startDiscoPingLocked(purpose discoPingPurpose, ...) {
    if purpose != pingCLI {
        st := de.endpointState[ep]
        if st != nil {
            now := mono.Now()
            if now.Before(st.lastPing.Add(discoPingInterval)) {
                return  // Rate limited: < 5 seconds since last ping
            }
        }
    }
}
```

**Key patterns to replicate:**
1. Per-endpoint `lastPing` timestamp - skip if < 5s ago
2. Per-peer `lastSend` timestamp - stop heartbeats if peer idle > 45s
3. `trustUDPAddrDuration` (6.5s) - don't re-probe confirmed paths
4. Separate `discoPingPurpose` types (heartbeat, discovery, CLI)

## STUN Async Pattern

### Go source: `net/netcheck/netcheck.go`

```go
func (c *Client) GetReport(ctx context.Context, dm *tailcfg.DERPMap, ...) (*Report, error) {
    // ... setup ...
    for _, p := range plan.Probes {
        go rs.probeUDP(ctx, p)  // Each probe is async
    }
    // Wait for results or timeout
    select {
    case <-ctx.Done():
    case <-rs.waitCh:
    }
}
```

Each STUN probe:
1. Sends binding request
2. Registers TxID in `inFlight` map with callback
3. Returns immediately
4. Callback fires when response arrives (matched by TxID in receive path)
5. 100ms retransmit timer resends if no response

**ESP32 mapping:** Send STUN request from `coord` task. Register expected TxID.
`net_io` task receives STUN response in select() loop, routes to `stun_rx_queue`.
`coord` task checks queue, matches TxID.

## Packet Classification (MagicSock)

### Go source: `wgengine/magicsock/magicsock.go`

Every received UDP packet is classified:

```go
func packetLooksLike(b []byte) packetType {
    if len(b) >= 20 && b[0] == 0x01 && b[1] == 0x01 {
        return packetSTUN  // STUN binding response
    }
    if len(b) >= disco.HeaderSize && disco.LooksLikeDiscoWrapper(b) {
        return packetDisco  // Has "TS" + emoji magic bytes
    }
    return packetWireGuard  // Everything else -> WireGuard
}
```

DISCO magic: `"TS\xf0\x9f\x92\xac"` (6 bytes = "TS" + sparkles emoji in UTF-8)

**ESP32 mapping:** `net_io` task classifies each UDP packet using same logic,
routes to appropriate queue. This is the core multiplexing pattern.

## Wire Protocol Sizes (for buffer planning)

| Protocol | Overhead | Typical Payload | Total |
|----------|----------|-----------------|-------|
| DISCO envelope | 62 bytes (6 magic + 32 key + 24 nonce) | 44-130 bytes | ~106-192 bytes |
| DERP frame | 5 bytes (1 type + 4 length) | up to 64KB | ~1500 typical |
| Noise transport | 3 bytes (1 type + 2 length) + 16 MAC | variable | variable |
| WireGuard | 32 bytes (handshake init) | up to 1500 | ~1500 typical |
| STUN | 20 bytes header | 0-100 bytes attrs | ~48-96 bytes |
| HTTP/2 frame | 9 bytes | variable | variable |
