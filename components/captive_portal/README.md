# ESPHome captive portal compatibility patch

Imported from ESPHome **2026.9.1**:
https://github.com/esphome/esphome/tree/2026.9.1/esphome/components/captive_portal

EspControl uses this version's standard captive portal and page. Its setup uses
IPv4, while Espframe also enables IPv6 for station connections. The upstream DNS
server chooses an IPv6 socket but receives client addresses into `sockaddr_in`.
This patch explicitly creates and binds an IPv4 socket for setup AP DNS.

`portal.html` was decoded from the gzip array in that version's `captive_index.h`;
its original SHA-256 is
`98ff9b7ed031268f7dd6483d90b7f0eed6e4cf689cdb14fe4eb5a59b22d8ad37`.
The layout now uses Espframe's webserver cards, labels, inputs and buttons.
`npm run generate` embeds `docs/webserver/src/style.css` at the template's style
marker and produces gzip and Brotli versions of `captive_index.h` and `wifi_saved.h`. Everything
is served locally, with no stylesheet or font download during hotspot setup.
The setup page shows the network list, SSID/password fields and Save button.
Device/MAC headings and the OTA upload panel are omitted. The upstream script
retains provisioning behavior, prevents default `href="#"` navigation when
choosing a network, omits updates to the removed headings, and keeps the page title as EspFrame WiFi setup. Browser coverage
checks these focused script changes, shared styles, narrow layouts and form behavior.

Page and scan responses use EspControl's no-cache policy. Operating-system
probes receive the standard HTTP 200 portal page; no custom redirects are added.
Credential persistence, scan filtering and the `/config.json`, `/wifisave` and
`/update` endpoints retain ESPHome's behavior. `/wifisave` returns a locally
embedded HTML confirmation with instructions to reconnect to home WiFi and
continue setup on the frame. It needs no scripts, redirects or network requests.

C/C++ files use ESPHome's GPLv3 license; Python uses its MIT license (`LICENSE`).
The page originated in esphome/esphome-webserver under MIT (`PORTAL_LICENSE`).
Recheck these patches when upgrading ESPHome and remove the local component
once the corresponding upstream fixes are available.
