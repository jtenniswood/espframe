# MicroLink

Vendored from `https://github.com/CamM2325/microlink`.

Pinned commit: `216da3300f0493b0860247d43f7af5ce29df63a5`

This code is used only by the ESPFrame Tailscale proof-of-concept build. The
normal factory and release builds do not include it.

Local patch:
- `components/microlink/CMakeLists.txt` only requires `esp_http_server` and
  `esp_driver_tsens` when Microlink's own HTTP config server is enabled.
