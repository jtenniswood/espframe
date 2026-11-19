---
title: Connect EspFrame to Immich Over a WireGuard VPN
description: Set up a WireGuard tunnel from the frame's web UI so a digital photo frame can reach a private Immich server from another network.
---

# Connect EspFrame to Immich Over a WireGuard VPN

Use WireGuard when the frame is not on the same network as your Immich server, for example a frame at a relative's house. The frame connects to your WireGuard server (usually your home router), and Immich stays reachable at its normal home network address.

WireGuard is off by default. Nothing changes for frames that do not use it.

## What you need

- A WireGuard server you control, such as a router, with a free UDP port reachable from the internet.
- A new WireGuard peer on that server for the frame, using the frame's **public key** (see below).
- Immich reachable from the VPN, at an address inside the network you allow for the peer.

## Set up

1. Generate a key pair for the frame with any WireGuard tool, such as `wg genkey | tee frame.key | wg pubkey`.
2. On the server, add a peer using the frame's public key and allow only the frame's VPN address, for example `192.168.88.200/32`. Allow the VPN port through the server's firewall and let the VPN reach the Immich machine.
3. Open the web UI at `http://<device-ip>/` and expand **WireGuard VPN** on the Immich tab.
4. Fill in the fields, paste the frame's **private key**, and turn on **Connect through WireGuard**.
5. Press **Restart now**. WireGuard settings apply when the frame starts.

| Field | What to enter |
|---|---|
| **Server Address** | Public IP address or hostname of the WireGuard server, with no `http://` and no port. |
| **Server Port** | The server's WireGuard UDP port, `51820` by default. |
| **Frame Address** | The frame's address inside the VPN, matching the peer on the server. Choose an unused address in the same network as Immich. |
| **Network Mask** | The mask of the network Immich is on, for example `255.255.255.0`. |
| **Allowed IPs** | Networks reachable through the VPN, for example `192.168.88.0/24`. Up to four, separated by commas. |
| **Server Public Key** | The public key of the WireGuard interface on the server. |
| **Frame Private Key** | The private key matching the public key you gave the server. |

Keep the **Immich Server URL** set to Immich's normal address, such as `http://192.168.88.10:2283`.

## How traffic is routed

The frame only sends traffic through the tunnel when the destination is inside the **Frame Address** and **Network Mask** network. Everything else uses the normal Wi-Fi connection. Pick the address and mask so that your Immich server falls inside that network.

## Private key and backups

The private key is stored on the frame and is never shown again after saving. The web UI only shows whether a key is set, and you can replace or remove it. WireGuard settings, including the key, are **not** included in [backups](/backup), so enter them again on a restored or replacement frame.

## Limits

- IPv4 only, with one server peer.
- The frame needs the correct time before the tunnel can start, so it waits for the clock to sync.
- If the tunnel cannot be set up, for example because a field is missing or invalid, the frame logs the reason and carries on without it. It still starts normally and never reboots because the VPN is down.
- Turning WireGuard off in the web UI and restarting returns the frame to its normal behaviour.

## Checking the tunnel

When WireGuard is running, the card shows whether the tunnel is **Connected** and how long ago the last handshake was. It is read-only and appears once the frame reports it. A saved setting that has not been applied yet shows "restart to apply".

While the tunnel is enabled, the frame also writes a status line to the **Logs** tab about every 30 seconds, and when an Immich request starts or fails. The line shows:

- whether the Immich server is a hostname or an IPv4 address, and the address it resolved to;
- which network interface the frame uses to reach that address (`wg` is the tunnel, `st` is Wi-Fi);
- whether that address is inside the tunnel's netmask and allowed IPs.

If the address is outside both, traffic does not use the tunnel. This commonly happens when the Immich hostname resolves to a public address on another network instead of your home network address.

If the frame joins a network that uses the same addresses as the Immich network, it keeps the tunnel interface behind Wi-Fi so local traffic stays local.

## Troubleshooting

- Check the frame's logs on the **Logs** tab for lines starting with `wireguard`.
- If the log says a field is "missing or invalid", fix that field and restart.
- If the tunnel starts but Immich does not load, confirm the server allows the frame's VPN address to reach Immich and that Immich is inside the Network Mask.
- See [Troubleshooting](/troubleshooting) for general connection problems.
