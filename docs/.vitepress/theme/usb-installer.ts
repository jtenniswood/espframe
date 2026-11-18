// Browser-only: importing ESP Web Tools registers DOM custom elements.
// Bundle the pinned installer so the esptool-js override in package.json applies.
// Version 0.7.1 retains the ESP32-P4 v3.1/v3.2 flash-power initialization fix:
// https://github.com/espressif/esptool-js/pull/268
// ESP Web Tools 10.5.2 imports stylesheets from Material Web 2.5.0.
export async function loadUsbInstaller() {
  await import('esp-web-tools/dist/install-button.js')
}
