// Browser-only: importing ESP Web Tools registers DOM custom elements.
// Bundle the pinned installer so the esptool-js override in package.json applies.
// Version 0.7.0 includes the ESP32-P4 v3.1/v3.2 flash-power initialization fix:
// https://github.com/espressif/esptool-js/pull/268
// Material Web 2.4.1 retains the stylesheet import paths used by ESP Web Tools.
export async function loadUsbInstaller() {
  await import('esp-web-tools/dist/install-button.js')
}
