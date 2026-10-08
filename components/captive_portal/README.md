# ESPHome captive portal compatibility patch

Imported from ESPHome **2026.9.1**:
https://github.com/esphome/esphome/tree/2026.9.1/esphome/components/captive_portal

EspControl uses this version's standard captive portal and page. Its setup uses
IPv4, while Espframe also enables IPv6 for station connections. The upstream DNS
server chooses an IPv6 socket but receives client addresses into `sockaddr_in`.
This patch explicitly creates and binds an IPv4 socket for setup AP DNS.

The page layout, labels, icons and forms are the upstream page. `portal.html` was
decoded from the gzip array in that version's `captive_index.h`; its original
SHA-256 is `98ff9b7ed031268f7dd6483d90b7f0eed6e4cf689cdb14fe4eb5a59b22d8ad37`.
The only page change prevents the default `href="#"` navigation when choosing a
network, preserving the form contents. `npm run generate` produces both gzip and
Brotli versions of `captive_index.h` from this source.

Page and scan responses use EspControl's no-cache policy. Operating-system
probes receive the standard HTTP 200 portal page; no custom redirects are added.
Credential persistence, scan filtering and the `/config.json`, `/wifisave` and
`/update` endpoints retain ESPHome's behavior.

C/C++ files use ESPHome's GPLv3 license; Python uses its MIT license (`LICENSE`).
The page originated in esphome/esphome-webserver under MIT (`PORTAL_LICENSE`).
Recheck these patches when upgrading ESPHome and remove the local component
once the corresponding upstream fixes are available.
