const assert = require("assert/strict");
const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const template = fs.readFileSync(path.join(root, "docs/webserver/src/app.template.ts"), "utf8");
const publicApp = fs.readFileSync(path.join(root, "docs/public/webserver/app.js"), "utf8");
const endpointsSource = fs.readFileSync(path.join(root, "docs/webserver/src/endpoints.ts"), "utf8");
const backupImportSource = fs.readFileSync(path.join(root, "docs/webserver/src/backup_import.ts"), "utf8");
const immichFilterSource = fs.readFileSync(path.join(root, "common/addon/immich_filter.yaml"), "utf8");
const immichConfigSource = fs.readFileSync(path.join(root, "common/addon/immich_config.yaml"), "utf8");
const product = JSON.parse(fs.readFileSync(path.join(root, "product/espframe.json"), "utf8"));
const supportButtonImage = fs.readFileSync(
  path.join(root, "docs/webserver/src/buy_me_a_coffee_button.webp.b64"),
  "utf8"
).trim();

const modules = {
  "__ESPFRAME_WEB_CONTRACTS__": "web_contracts.ts",
  "__ESPFRAME_WEB_APP_SHELL__": "app_shell.ts",
  "__ESPFRAME_WEB_ENDPOINTS__": "endpoints.ts",
  "__ESPFRAME_WEB_RUNTIME_STATE__": "runtime_state.ts",
  "__ESPFRAME_WEB_STARTUP_WIZARD__": "startup_wizard.ts",
  "__ESPFRAME_WEB_SETTINGS_IMMICH_CARDS__": "settings_immich_cards.ts",
  "__ESPFRAME_WEB_SETTINGS_SCREEN_CARDS__": "settings_screen_cards.ts",
  "__ESPFRAME_WEB_SETTINGS_FIRMWARE_CARD__": "settings_firmware_card.ts",
  "__ESPFRAME_WEB_SETTINGS_CONTROLS__": "settings_controls.ts",
  "__ESPFRAME_WEB_LIVE_HELPERS__": "live_helpers.ts",
  "__ESPFRAME_WEB_BACKUP_IMPORT__": "backup_import.ts",
  "__ESPFRAME_WEB_COMPAT_HELPERS__": "compat.ts",
};

for (const [placeholder, filename] of Object.entries(modules)) {
  assert.ok(template.includes(placeholder), `${placeholder} must be present in app.template.ts`);
  const source = fs.readFileSync(path.join(root, "docs/webserver/src", filename), "utf8");
  assert.ok(source.trim().length > 0, `${filename} must not be empty`);
}

assert.equal(/__ESPFRAME_[A-Z0-9_]+__/.test(publicApp), false, "public app must not contain generator placeholders");
assert.match(publicApp, /function renderSettings\(\)/, "public app should include the settings renderer");
assert.match(publicApp, /function importConfig\(\)/, "public app should include backup import behavior");
assert.match(publicApp, /BACKUP_CONFIG_VERSION\s*=/, "public app should include generated backup version");
assert.match(publicApp, /BACKUP_SCHEMA\s*=/, "public app should include generated backup schema");
assert.match(publicApp, /function renderWizard\(\)/, "public app should include the startup wizard");
assert.ok(publicApp.includes("/espframe/api/v1/configuration"), "public app should use the versioned configuration API");
assert.ok(endpointsSource.includes("configurationUpdateQueue"), "configuration writes should be serialized");
assert.ok(endpointsSource.includes("configurationUpdateQueue = request.catch"), "the configuration queue should continue after a failed save");
assert.ok(publicApp.includes("customElements.define"), "public app should register its component root");
assert.ok(publicApp.includes('"album_order"'), "public app should include album order in photo-source apply keys");
assert.ok(publicApp.includes("Move album up"), "public app should include album reorder controls");
assert.ok(publicApp.includes("movePhotoIdRow"), "public app should keep photo ID and label rows reorderable");
assert.ok(
  publicApp.includes("All selected albums require Immich 3.2 or newer"),
  "flat-filter servers should explain why all-album matching is unavailable"
);
assert.ok(
  publicApp.includes("Choose Any to clear a saved value") &&
    publicApp.includes("String(optionEl.value) !== String(recoveryValue)"),
  "compatibility mode should allow a saved unsupported rating to be cleared"
);
assert.ok(
  publicApp.includes("disableEditing: !supportsStructured") &&
    publicApp.includes("allowClearLast: !supportsStructured") &&
    publicApp.includes("Saved exclusions can still be removed"),
  "compatibility mode should prevent new exclusions while allowing saved exclusions to be removed"
);
const filterFlush = immichFilterSource.slice(
  immichFilterSource.indexOf("- id: flush_slots_and_refetch"),
  immichFilterSource.indexOf("- id: auto_apply_photo_source")
);
assert.ok(
  filterFlush.includes("id(immich_request_state).empty_branch_attempts = 0"),
  "applying a photo filter should reset exhausted inclusion-branch attempts"
);
assert.ok(
  filterFlush.includes("filter_apply_pending = true") &&
    filterFlush.includes("filter_apply_pending = false") &&
    immichConfigSource.includes("return id(immich_request_state).filter_apply_pending") &&
    immichConfigSource.includes("script.execute: flush_slots_and_refetch"),
  "capability recovery should flush a photo-filter apply deferred by compatibility mode"
);
const legacyPreset = immichFilterSource.slice(
  immichFilterSource.indexOf("- id: apply_legacy_photo_source_preset"),
  immichFilterSource.indexOf("- id: flush_slots_and_refetch")
);
[
  "Match all enabled groups",
  "Any selected album",
  "Any selected person",
  "Any selected tag"
].forEach(function (defaultMode) {
  assert.ok(
    legacyPreset.includes('set_option(id(') && legacyPreset.includes('"' + defaultMode + '"'),
    "legacy photo-source presets should restore " + defaultMode
  );
});
const photoSourceApply = publicApp.slice(
  publicApp.indexOf("function applyPhotoSourceInputs()"),
  publicApp.indexOf("function schedulePhotoSourceApply")
);
assert.ok(
  photoSourceApply.indexOf("if (!vals) return;") < photoSourceApply.indexOf("pendingPhotoSourceSave = {"),
  "photo-source validation must preserve pending source changes until the first required ID is valid"
);
assert.match(supportButtonImage, /^UklGR/, "support button asset should be a base64-encoded WebP image");
assert.ok(
  publicApp.includes(`data:image/webp;base64,${supportButtonImage}`),
  "public app should embed the Buy Me a Coffee button image"
);
assert.equal(
  publicApp.includes(product.project.support_button_image_url),
  false,
  "embedded dashboard should not fetch the support button from a third party"
);
assert.ok(publicApp.includes('image.alt = "Buy Me A Coffee"'), "support button image should have accessible text");

const backupImportContext = { JSON };
require("vm").runInNewContext(backupImportSource, backupImportContext);
const connectionOnlyBackup = backupImportContext.migrateBackupConfig({
  version: 1,
  connection: { immich_url: "https://photos.example.com" }
});
assert.equal(
  Object.prototype.hasOwnProperty.call(connectionOnlyBackup, "photos"),
  false,
  "migrating a partial v1 backup must not synthesize omitted photo settings"
);
const displayOnlyBackup = backupImportContext.migrateBackupConfig({
  version: 1,
  photos: { display_mode: "Fit" }
});
assert.deepEqual(
  JSON.parse(JSON.stringify(displayOnlyBackup.photos)),
  { display_mode: "Fit" },
  "migrating a v1 photo group without a source must preserve the current photo filter"
);
const legacyAlbumBackup = backupImportContext.migrateBackupConfig({
  version: 1,
  photos: { source: "Album" }
});
assert.equal(legacyAlbumBackup.photos.albums_enabled, true, "legacy Album sources should enable the album group");
assert.equal(legacyAlbumBackup.photos.source, "Album", "legacy photo sources should remain available to firmware migration");

// The web server identifies each entity with name_id ("domain/Friendly Name") plus a
// legacy id ("domain-object_id"). ENTITY_STATE_MAP and the REST endpoints both use the
// name form, so live events must be resolved via name_id or nothing ever matches.
assert.ok(
  publicApp.includes("d.name_id"),
  "live state events should be resolved by name_id, not the legacy object-id form"
);

assert.ok(
  publicApp.includes("Promise.all([deviceCheck, publicCheck])"),
  "firmware checks should accept the public release index when the device update entity is slow or unavailable"
);
assert.ok(
  publicApp.includes("waitForFirmwareUpdateResponse(12)"),
  "firmware checks should wait for the asynchronous device update result instead of reading UNKNOWN once"
);

console.log("web module tests passed");
