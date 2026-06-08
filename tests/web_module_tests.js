const assert = require("assert/strict");
const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const template = fs.readFileSync(path.join(root, "docs/webserver/src/app.template.js"), "utf8");
const publicApp = fs.readFileSync(path.join(root, "docs/public/webserver/app.js"), "utf8");

const modules = {
  "__ESPFRAME_WEB_APP_SHELL__": "app_shell.js",
  "__ESPFRAME_WEB_ENDPOINTS__": "endpoints.js",
  "__ESPFRAME_WEB_RUNTIME_STATE__": "runtime_state.js",
  "__ESPFRAME_WEB_STARTUP_WIZARD__": "startup_wizard.js",
  "__ESPFRAME_WEB_SETTINGS_IMMICH_CARDS__": "settings_immich_cards.js",
  "__ESPFRAME_WEB_SETTINGS_SCREEN_CARDS__": "settings_screen_cards.js",
  "__ESPFRAME_WEB_SETTINGS_FIRMWARE_CARD__": "settings_firmware_card.js",
  "__ESPFRAME_WEB_SETTINGS_CONTROLS__": "settings_controls.js",
  "__ESPFRAME_WEB_LIVE_HELPERS__": "live_helpers.js",
  "__ESPFRAME_WEB_BACKUP_IMPORT__": "backup_import.js",
  "__ESPFRAME_WEB_COMPAT_HELPERS__": "compat.js",
};

for (const [placeholder, filename] of Object.entries(modules)) {
  assert.ok(template.includes(placeholder), `${placeholder} must be present in app.template.js`);
  const source = fs.readFileSync(path.join(root, "docs/webserver/src", filename), "utf8");
  assert.ok(source.trim().length > 0, `${filename} must not be empty`);
}

assert.equal(/__ESPFRAME_[A-Z0-9_]+__/.test(publicApp), false, "public app must not contain generator placeholders");
assert.ok(publicApp.includes("function renderSettings()"), "public app should include the settings renderer");
assert.ok(publicApp.includes("function importConfig()"), "public app should include backup import behavior");
assert.ok(publicApp.includes("var BACKUP_SCHEMA = "), "public app should include generated backup schema");
assert.ok(publicApp.includes("function renderWizard()"), "public app should include the startup wizard");

console.log("web module tests passed");
