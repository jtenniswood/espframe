const assert = require("assert/strict");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawn } = require("child_process");

const root = path.resolve(__dirname, "..");
const appSource = fs.readFileSync(path.join(root, "docs/public/webserver/app.js"), "utf8");
const product = JSON.parse(fs.readFileSync(path.join(root, "product/espframe.json"), "utf8"));
const expectedBackupGroups = product.project.backup_export_groups;
const expectedBackupFields = product.project.backup_export_fields;
const configurationKeyByEndpointName = {};
for (const setting of product.settings) {
  configurationKeyByEndpointName[setting.entity.name] = setting.key;
}
for (const collectionName of ["web_static_entities", "web_manual_entities"]) {
  for (const [key, spec] of Object.entries(product.project[collectionName] || {})) {
    const entity = String(spec.entity || "");
    const slash = entity.indexOf("/");
    const domain = slash === -1 ? "" : entity.slice(0, slash);
    if (["number", "select", "switch", "text"].includes(domain)) {
      configurationKeyByEndpointName[entity.slice(slash + 1)] = key;
    }
  }
}
const smokeAlbumIds = [
  "11111111-1111-4111-8111-111111111111",
  "44444444-4444-4444-8444-444444444444",
];
const smokeAlbumLabels = ["Family", "Travel"];

function findExecutable(name) {
  const pathDirs = String(process.env.PATH || "")
    .split(path.delimiter)
    .filter(Boolean);
  for (const dir of pathDirs) {
    const candidate = path.join(dir, name);
    if (fs.existsSync(candidate)) return candidate;
  }
  return "";
}

function resolveChromePath() {
  const candidates = [
    process.env.CHROME_BIN,
    process.env.CHROME_PATH,
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
    "/usr/bin/google-chrome",
    "/usr/bin/google-chrome-stable",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
    findExecutable("google-chrome"),
    findExecutable("google-chrome-stable"),
    findExecutable("chromium"),
    findExecutable("chromium-browser"),
  ];
  return candidates.find((candidate) => candidate && fs.existsSync(candidate)) || "";
}

const chromePathCandidates = [
  process.env.CHROME_BIN,
  process.env.CHROME_PATH,
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
  "/Applications/Chromium.app/Contents/MacOS/Chromium",
  "/usr/bin/google-chrome",
  "/usr/bin/google-chrome-stable",
  "/usr/bin/chromium",
  "/usr/bin/chromium-browser",
  "google-chrome",
  "google-chrome-stable",
  "chromium",
  "chromium-browser",
].filter(Boolean);
const chromePath = resolveChromePath();

function requireChromePath() {
  if (!chromePath) {
    throw new Error(`Google Chrome or Chromium is required for browser smoke tests. Checked: ${chromePathCandidates.join(", ")}`);
  }
  return chromePath;
}

function chromeSandboxArgs() {
  if (process.platform !== "linux") return [];
  if (typeof process.getuid !== "function" || process.getuid() !== 0) return [];
  return ["--no-sandbox"];
}

const validBackupFixture = {
  version: 1,
  connection: {
    immich_url: "https://imported.photos.example.com",
    api_key: "imported-api-key",
  },
  photos: {
    source: "Person",
    album_ids: "11111111-1111-4111-8111-111111111111",
    album_labels: "Family",
    person_ids: "22222222-2222-4222-8222-222222222222",
    person_labels: "Alex",
    tag_ids: "33333333-3333-4333-8333-333333333333",
    tag_labels: "Espframe",
    date_filter_enabled: true,
    date_filter_mode: "Relative Range",
    relative_amount: 3,
    relative_unit: "Years",
    orientation: "Portrait Only",
    portrait_pairing: true,
    portrait_pairing_range: "Within 2 Days",
    portrait_pairs_only: true,
    display_mode: "Fill",
  },
  frequency: {
    interval: "30 seconds",
    conn_timeout: "5 minutes",
  },
  firmware_updates: {
    auto_update: true,
    update_frequency: "Weekly",
    wifi_auto_update: true,
  },
  clock: {
    show: true,
    format: "24 Hour",
    timezone: "Europe/London (GMT+0)",
    ntp_servers: ["0.pool.ntp.org", "1.pool.ntp.org", "2.pool.ntp.org"],
  },
  screen: {
    brightness_day: 90,
    brightness_night: 60,
    schedule_enabled: true,
    schedule_on_hour: 7,
    schedule_off_hour: 22,
    schedule_wake_timeout: 120,
    base_tone_enabled: true,
    base_tone: 35,
    warm_tones_enabled: true,
    warm_tone_intensity: 45,
    warm_tone_override: false,
    rotation: "180",
  },
};

const rejectedBackupFixture = {
  version: 1,
  photos: {
    album_ids: "not-an-album-uuid",
  },
};

const partialBackupFixture = {
  version: 1,
  connection: {
    immich_url: "https://partial-import.photos.example.com",
  },
  photos: {
    album_ids: "not-an-album-uuid",
  },
};

const missingVersionBackupFixture = {
  photos: {
    source: "Album",
  },
};

const futureVersionBackupFixture = {
  version: 2,
  connection: {
    immich_url: "https://future.photos.example.com",
  },
};

const unsupportedVersionBackupFixture = {
  version: 0,
  connection: {
    immich_url: "https://unsupported.photos.example.com",
  },
};

const scenarios = [
  { name: "wizard", configured: false, width: 1280, height: 900 },
  { name: "wizard-connection-save", configured: false, width: 1280, height: 900 },
  { name: "settings", configured: true, width: 1280, height: 900 },
  { name: "settings-mobile", configured: true, width: 390, height: 900 },
  { name: "firmware-main-install", configured: true, width: 1280, height: 900 },
  { name: "firmware-c6-install", configured: true, width: 1280, height: 900 },
  { name: "firmware-rollback", configured: true, width: 1280, height: 900 },
  { name: "firmware-rollback-failure", configured: true, width: 1280, height: 900, firmwareUploadFails: true },
  { name: "firmware-index-unavailable", configured: true, width: 1280, height: 900, firmwareIndexUnavailable: true },
  { name: "firmware-newer-prerelease", configured: true, width: 1280, height: 900, installedFirmwareVersion: "v1.2.0-beta.1" },
  ...(product.devices[1]
    ? [{ name: "firmware-second-device-index", configured: true, width: 1280, height: 900, firmwareDeviceSlug: product.devices[1].slug }]
    : []),
  { name: "photo-source-reorder", configured: true, width: 1280, height: 900 },
  { name: "screen-rotation-developer", configured: true, width: 1280, height: 900, query: "dev=experimental" },
  { name: "screen-tone-schedule", configured: true, width: 1280, height: 900 },
  { name: "daily-settings-controls", configured: true, width: 1280, height: 900 },
  { name: "backup-import-success", configured: true, width: 1280, height: 900, importFixture: validBackupFixture },
  { name: "backup-import-save-failure", configured: true, width: 1280, height: 900, importFixture: validBackupFixture, failedPostEndpoint: "Screen: Daytime Brightness" },
  { name: "backup-import-partial", configured: true, width: 1280, height: 900, importFixture: partialBackupFixture },
  { name: "backup-import-rejected", configured: true, width: 1280, height: 900, importFixture: rejectedBackupFixture },
  { name: "backup-import-missing-version", configured: true, width: 1280, height: 900, importFixture: missingVersionBackupFixture },
  { name: "backup-import-future-version", configured: true, width: 1280, height: 900, importFixture: futureVersionBackupFixture },
  { name: "backup-import-unsupported-version", configured: true, width: 1280, height: 900, importFixture: unsupportedVersionBackupFixture },
];

function browserScriptForScenario(scenario) {
  const firmwareDeviceSlug = scenario.firmwareDeviceSlug || product.devices[0].slug;
  const installedFirmwareVersion = scenario.installedFirmwareVersion || "v1.0.0";
  return `
    window.__smoke = {
      posts: [],
      postRecords: [],
      errors: [],
      fetchedUrls: [],
      downloads: 0,
      exportPayloads: [],
      inputClicks: 0,
      importFixture: ${JSON.stringify(scenario.importFixture || null)},
      failedPostEndpoint: ${JSON.stringify(scenario.failedPostEndpoint || "")},
      firmwareIndexUnavailable: ${JSON.stringify(!!scenario.firmwareIndexUnavailable)},
      firmwareUploadFails: ${JSON.stringify(!!scenario.firmwareUploadFails)}
    };
    window.addEventListener("error", function (event) {
      window.__smoke.errors.push(event.message || "browser error");
    });
    window.addEventListener("unhandledrejection", function (event) {
      window.__smoke.errors.push(String(event.reason || "unhandled rejection"));
    });

    const NativeBlob = window.Blob;
    window.Blob = class SmokeBlob extends NativeBlob {
      constructor(parts, options) {
        super(parts, options);
        this.__smokeText = (parts || []).map((part) => typeof part === "string" ? part : "").join("");
      }
    };

    URL.createObjectURL = function (blob) {
      if (blob && typeof blob.__smokeText === "string") window.__smoke.exportPayloads.push(blob.__smokeText);
      return "blob:espframe-smoke";
    };
    URL.revokeObjectURL = function () {};
    HTMLAnchorElement.prototype.click = function () {
      if (this.download) window.__smoke.downloads += 1;
    };
    FileReader.prototype.readAsText = function (file) {
      this.result = file && file.__smokeContent ? file.__smokeContent : "";
      if (this.onload) setTimeout(() => this.onload({ target: this }), 0);
    };
    HTMLInputElement.prototype.click = function () {
      if (this.type !== "file") return;
      window.__smoke.inputClicks += 1;
      if (!window.__smoke.importFixture) return;
      Object.defineProperty(this, "files", {
        configurable: true,
        value: [{ name: "espframe-config-smoke.json", __smokeContent: JSON.stringify(window.__smoke.importFixture) }]
      });
      setTimeout(() => this.dispatchEvent(new Event("change")), 0);
    };

    class SmokeEventSource {
      constructor(url) {
        this.url = url;
        this.listeners = {};
        setTimeout(() => {
          if (this.onopen) this.onopen({ type: "open" });
          this.dispatch("log", { msg: "Smoke log line", lvl: 3 });
          this.dispatch("state", { id: "text_sensor/Screen: Sunrise", value: "06:30" });
          this.dispatch("state", { id: "text_sensor/Screen: Sunset", value: "21:45" });
          this.dispatch("state", { id: "text_sensor/Firmware: Version", value: ${JSON.stringify(installedFirmwareVersion)} });
          this.dispatch("state", { id: "text_sensor/Firmware: Device", value: ${JSON.stringify(firmwareDeviceSlug)} });
          this.dispatch("state", { id: "text_sensor/ESP32-C6: Current Firmware", value: "2.0.0" });
          this.dispatch("state", { id: "text_sensor/ESP32-C6: Available Firmware", value: "2.0.1" });
          this.dispatch("state", { id: "text_sensor/ESP32-C6: Update Available", value: "Update available" });
        }, 25);
      }
      addEventListener(type, listener) {
        if (!this.listeners[type]) this.listeners[type] = [];
        this.listeners[type].push(listener);
      }
      dispatch(type, data) {
        (this.listeners[type] || []).forEach((listener) => {
          listener({ data: JSON.stringify(data) });
        });
      }
      close() {}
    }
    window.EventSource = SmokeEventSource;

    const configured = ${JSON.stringify(scenario.configured)};
    const endpointValues = {
      "Connection: Server URL": configured ? "https://photos.example.com" : "",
      "Connection: API Key": configured ? "fixture-api-key" : "",
      "Firmware: Version": ${JSON.stringify(installedFirmwareVersion)},
      "Firmware: Device": ${JSON.stringify(firmwareDeviceSlug)},
      "Photos: Source": "Album",
      "Photos: Album IDs": ${JSON.stringify(smokeAlbumIds.join(","))},
      "Photos: Album Labels": ${JSON.stringify(smokeAlbumLabels.join(","))},
      "Photos: Person IDs": "22222222-2222-4222-8222-222222222222",
      "Photos: Person Labels": "Alex",
      "Photos: Tag IDs": "33333333-3333-4333-8333-333333333333",
      "Photos: Tag Labels": "Espframe",
      "Photos: Date Filter": true,
      "Photos: Date Filter Mode": "Fixed Range",
      "Photos: Date From": "2024-01-01",
      "Photos: Date To": "2026-06-07",
      "Photos: Relative Amount": 2,
      "Photos: Relative Unit": "Years",
      "Photos: Orientation": "Any",
      "Photos: Portrait Pairing": true,
      "Photos: Portrait Pairing Range": "Same Day",
      "Photos: Paired Portraits Only": false,
      "Photos: Display Mode": "Fill",
      "Photos: Slideshow Interval": "15 seconds",
      "Device: Metadata Date": true,
      "Device: Metadata Location": true,
      "Device: Metadata Date Format": "Date Taken",
      "Device: Metadata Date Taken Format": "1 January, 2026",
      "Screen: Connection Timeout": "10 minutes",
      "Clock: Show": true,
      "Clock: Format": "24 Hour",
      "Clock: Timezone": "Europe/London (GMT+0)",
      "Clock: NTP Server 1": "0.pool.ntp.org",
      "Clock: NTP Server 2": "1.pool.ntp.org",
      "Clock: NTP Server 3": "2.pool.ntp.org",
      "Firmware: Auto Update": true,
      "Firmware: Update Frequency": "Daily",
      "WiFi Firmware: Auto Update": true,
      "ESP32-C6: Current Firmware": "2.0.0",
      "ESP32-C6: Available Firmware": "2.0.1",
      "ESP32-C6: Update Available": "Update available",
      "Screen: Daytime Brightness": 100,
      "Screen: Nighttime Brightness": 75,
      "Screen: Schedule Enabled": false,
      "Screen: Schedule On Hour": 6,
      "Screen: Schedule Off Hour": 23,
      "Screen: Schedule Wake Timeout": 60,
      "Screen: Tone Adjustment": false,
      "Screen: Display Tone": 0,
      "Screen: Night Tone Adjustment": false,
      "Screen: Warm Tone Intensity": 50,
      "Screen: Warm Tone Override": false,
      "Screen: Rotation": "0",
      "Developer: Features": false
    };
    const configurationKeyByEndpointName = ${JSON.stringify(configurationKeyByEndpointName)};
    const configurationEndpointNameByKey = Object.fromEntries(
      Object.entries(configurationKeyByEndpointName).map(([name, key]) => [key, name])
    );

    function configurationSnapshotValues() {
      const values = {};
      Object.entries(configurationEndpointNameByKey).forEach(([key, name]) => {
        if (Object.prototype.hasOwnProperty.call(endpointValues, name)) values[key] = endpointValues[name];
      });
      return values;
    }

    function endpointNameForUrl(decoded) {
      return Object.keys(endpointValues)
        .sort((left, right) => right.length - left.length)
        .find((name) => decoded.indexOf(name) !== -1) || "";
    }

    function requestParam(decoded, body, name) {
      const queryIndex = decoded.indexOf("?");
      const query = queryIndex === -1 ? "" : decoded.slice(queryIndex + 1);
      const params = new URLSearchParams(body || query);
      return params.get(name);
    }

    function updateEndpointValueFromPost(decoded, body) {
      const endpointName = endpointNameForUrl(decoded);
      if (!endpointName) return;
      if (decoded.indexOf("/turn_on") !== -1) {
        endpointValues[endpointName] = true;
      } else if (decoded.indexOf("/turn_off") !== -1) {
        endpointValues[endpointName] = false;
      } else if (decoded.indexOf("/set") !== -1) {
        const option = requestParam(decoded, body, "option");
        const value = requestParam(decoded, body, "value");
        if (option !== null) endpointValues[endpointName] = option;
        else if (value !== null) endpointValues[endpointName] = value;
      }
    }

    window.fetch = function (url, options) {
      const method = options && options.method ? options.method : "GET";
      const decoded = decodeURIComponent(String(url));
      window.__smoke.fetchedUrls.push(decoded);
      const body = options && options.body != null ? String(options.body) : "";
      if (decoded.indexOf("versions.json") !== -1) {
        if (window.__smoke.firmwareIndexUnavailable) {
          return Promise.resolve({ ok: false, status: 404, json: () => Promise.resolve({}) });
        }
        return Promise.resolve({
          ok: true,
          status: 200,
          json: () => Promise.resolve({
            device: ${JSON.stringify(firmwareDeviceSlug)},
            versions: [
              { version: "not-a-version", ota: { path: "bad.ota.bin", md5: "bad" } },
              { version: "v1.0.1", release_url: "https://github.com/jtenniswood/espframe/releases/tag/v1.0.1", ota: { path: ${JSON.stringify(firmwareDeviceSlug + ".ota.bin")}, md5: "11111111111111111111111111111111" } },
              { version: "v1.0.0", release_url: "https://github.com/jtenniswood/espframe/releases/tag/v1.0.0", ota: { path: "versions/v1.0.0/${firmwareDeviceSlug}.ota.bin", md5: "22222222222222222222222222222222" } },
              { version: "v0.9.0", release_url: "https://github.com/jtenniswood/espframe/releases/tag/v0.9.0", ota: { path: "versions/v0.9.0/${firmwareDeviceSlug}.ota.bin", md5: "33333333333333333333333333333333" } }
            ]
          })
        });
      }
      if (decoded.indexOf(".ota.bin") !== -1 && method === "GET") {
        return Promise.resolve({ ok: true, status: 200, blob: () => Promise.resolve(new Blob(["firmware"])) });
      }
      if (decoded === "/update" && method === "POST") {
        window.__smoke.posts.push(decoded);
        window.__smoke.postRecords.push({ url: decoded, body });
        return Promise.resolve({
          ok: !window.__smoke.firmwareUploadFails,
          status: window.__smoke.firmwareUploadFails ? 500 : 200,
          text: () => Promise.resolve(window.__smoke.firmwareUploadFails ? "Update failed" : "Update successful")
        });
      }
      if (decoded === "/espframe/api/v1/configuration") {
        if (method === "GET") {
          return Promise.resolve({
            ok: true,
            status: 200,
            json: () => Promise.resolve({ api_version: 1, values: configurationSnapshotValues(), unavailable: [] })
          });
        }
        if (window.__smoke.configurationUpdateInFlight) {
          return Promise.resolve({
            ok: false,
            status: 409,
            json: () => Promise.resolve({ api_version: 1, status: "rejected", error: "update_in_progress" })
          });
        }
        window.__smoke.configurationUpdateInFlight = true;
        const encoded = new URLSearchParams(body).get("configuration");
        const update = encoded ? JSON.parse(encoded) : { values: {} };
        const failedKey = configurationKeyByEndpointName[window.__smoke.failedPostEndpoint];
        if (failedKey && Object.prototype.hasOwnProperty.call(update.values, failedKey)) {
          window.__smoke.posts.push(decoded);
          window.__smoke.postRecords.push({ url: decoded, body });
          return new Promise((resolve) => setTimeout(() => {
            window.__smoke.configurationUpdateInFlight = false;
            resolve({
              ok: false,
              status: 422,
              json: () => Promise.resolve({ api_version: 1, status: "rejected", error: "smoke_failure", field: failedKey })
            });
          }, 0));
        }
        window.__smoke.posts.push(decoded);
        window.__smoke.postRecords.push({ url: decoded, body });
        return new Promise((resolve) => setTimeout(() => {
          Object.entries(update.values || {}).forEach(([key, value]) => {
            const endpointName = configurationEndpointNameByKey[key];
            if (endpointName) endpointValues[endpointName] = value;
          });
          window.__smoke.configurationUpdateInFlight = false;
          resolve({
            ok: true,
            status: 202,
            json: () => Promise.resolve({ api_version: 1, status: "accepted", updated: Object.keys(update.values || {}).length })
          });
        }, 0));
      }
      if (method === "POST") {
        window.__smoke.posts.push(decoded);
        window.__smoke.postRecords.push({ url: decoded, body });
        if (window.__smoke.failedPostEndpoint && decoded.indexOf(window.__smoke.failedPostEndpoint) !== -1) {
          return Promise.resolve({ ok: false, status: 500, json: () => Promise.resolve({}) });
        }
        updateEndpointValueFromPost(decoded, body);
      }
      if (decoded.indexOf("Firmware: Update") !== -1) {
        return Promise.resolve({
          ok: true,
          status: 200,
          json: () => Promise.resolve({ value: "v1.0.1", state: "UPDATE AVAILABLE", current_version: ${JSON.stringify(installedFirmwareVersion)}, latest_version: "v1.0.1" })
        });
      }
      const endpointName = endpointNameForUrl(decoded);
      const value = endpointName ? endpointValues[endpointName] : "";
      const state = value === true ? "ON" : value === false ? "OFF" : String(value);
      return Promise.resolve({
        ok: true,
        status: 200,
        json: () => Promise.resolve({ value, state, option: [] })
      });
    };
  `;
}

function smokeAssertionsForScenario(scenario) {
  const expectedFirmwareVersionsPath = scenario.firmwareDeviceSlug
    ? String(product.devices.find((device) => device.slug === scenario.firmwareDeviceSlug).public_manifest)
        .replace(/[^/]+$/, "versions.json")
    : "";
  return `
    (async function () {
      function waitFor(check, timeoutMs, label) {
        const started = Date.now();
        return new Promise((resolve, reject) => {
          function poll() {
            try {
              if (check()) return resolve();
            } catch (error) {
              return reject(error);
            }
            if (Date.now() - started > timeoutMs) return reject(new Error("Timed out waiting for " + label));
            setTimeout(poll, 50);
          }
          poll();
        });
      }
      function pageText() {
        return document.body.innerText || "";
      }
      function requireText(text) {
        if (pageText().indexOf(text) === -1) throw new Error("Missing text: " + text);
      }
      function buttonByText(text) {
        return Array.from(document.querySelectorAll("button")).find((button) => button.textContent.trim() === text);
      }
      function clickButton(text) {
        const button = buttonByText(text);
        if (!button) throw new Error("Button not found: " + text);
        button.click();
        return button;
      }
      function clickTab(text) {
        const tab = Array.from(document.querySelectorAll(".sp-tab")).find((item) => item.textContent.trim() === text);
        if (!tab) throw new Error("Tab not found: " + text);
        tab.click();
        return tab;
      }
      function requirePostContains(label, fragment, extraFragment) {
        const configured = latestConfigurationValue(fragment);
        if (configured.found) {
          let matches = true;
          if (extraFragment === "turn_on") matches = configured.value === true;
          else if (extraFragment === "turn_off") matches = configured.value === false;
          else if (extraFragment && extraFragment.indexOf("option=") === 0) matches = String(configured.value) === extraFragment.slice(7);
          else if (extraFragment && extraFragment.indexOf("value=") === 0) matches = String(configured.value) === extraFragment.slice(6);
          if (matches) return;
        }
        const found = window.__smoke.posts.some((url) =>
          url.indexOf(fragment) !== -1 && (!extraFragment || url.indexOf(extraFragment) !== -1)
        );
        if (!found) throw new Error(label + " was not posted to the device");
      }
      function configurationUpdates() {
        return window.__smoke.postRecords.map((record) => {
          if (record.url !== "/espframe/api/v1/configuration") return null;
          const encoded = new URLSearchParams(record.body).get("configuration");
          return encoded ? JSON.parse(encoded).values : null;
        }).filter(Boolean);
      }
      function latestConfigurationValue(fragment) {
        const endpoint = Object.keys(configurationKeyByEndpointName).find((name) => name.indexOf(fragment) !== -1);
        const key = endpoint && configurationKeyByEndpointName[endpoint];
        if (!key) return { found: false };
        const updates = configurationUpdates();
        for (let i = updates.length - 1; i >= 0; i--) {
          if (Object.prototype.hasOwnProperty.call(updates[i], key)) return { found: true, value: updates[i][key] };
        }
        return { found: false };
      }
      function hasConfigurationPost(fragment) {
        return latestConfigurationValue(fragment).found;
      }
      function latestPostRecord(fragment) {
        for (let i = window.__smoke.postRecords.length - 1; i >= 0; i--) {
          const record = window.__smoke.postRecords[i];
          if (record.url.indexOf(fragment) !== -1) return record;
        }
        throw new Error("POST record not found: " + fragment);
      }
      function postRecordParam(record, name) {
        const queryIndex = record.url.indexOf("?");
        const query = queryIndex === -1 ? "" : record.url.slice(queryIndex + 1);
        const params = new URLSearchParams(record.body || query);
        return params.get(name);
      }
      function requireLatestPostValue(label, fragment, expected) {
        const configured = latestConfigurationValue(fragment);
        const actual = configured.found ? String(configured.value) : postRecordParam(latestPostRecord(fragment), "value");
        if (actual !== expected) {
          throw new Error(label + " saved " + JSON.stringify(actual) + " instead of " + JSON.stringify(expected));
        }
      }
      function requireLatestPostParam(label, fragment, param, expected) {
        const configured = latestConfigurationValue(fragment);
        const actual = configured.found ? String(configured.value) : postRecordParam(latestPostRecord(fragment), param);
        if (actual !== expected) {
          throw new Error(label + " saved " + JSON.stringify(actual) + " instead of " + JSON.stringify(expected));
        }
      }
      function requireExportShape() {
        if (!window.__smoke.exportPayloads.length) throw new Error("Export payload was not captured");
        const exported = JSON.parse(window.__smoke.exportPayloads[0]);
        if (exported.version !== ${JSON.stringify(product.project.backup_config_version)}) {
          throw new Error("Exported backup version changed");
        }
        if (!exported.exported_at || typeof exported.exported_at !== "string") {
          throw new Error("Exported backup timestamp missing");
        }
        const expectedGroups = ${JSON.stringify(expectedBackupGroups)};
        const expectedFields = ${JSON.stringify(expectedBackupFields)};
        const actualGroups = Object.keys(exported).filter((key) => key !== "version" && key !== "exported_at");
        if (JSON.stringify(actualGroups) !== JSON.stringify(expectedGroups)) {
          throw new Error("Exported backup groups changed: " + JSON.stringify(actualGroups));
        }
        expectedGroups.forEach((group) => {
          const actualFields = Object.keys(exported[group] || {});
          const expectedGroupFields = expectedFields[group] || [];
          if (JSON.stringify(actualFields) !== JSON.stringify(expectedGroupFields)) {
            throw new Error("Exported backup fields changed for " + group + ": " + JSON.stringify(actualFields));
          }
        });
        if (!Array.isArray(exported.clock.ntp_servers) || exported.clock.ntp_servers.length !== 3) {
          throw new Error("Exported NTP servers must remain a three-item array");
        }
        if (exported.screen.schedule_wake_timeout !== 60) {
          throw new Error("Exported schedule wake timeout was not normalized");
        }
      }
      function selectByLabel(labelText) {
        const labels = Array.from(document.querySelectorAll("label"));
        const label = labels.find((item) => item.textContent.trim() === labelText);
        if (!label || !label.parentElement) throw new Error("Field not found: " + labelText);
        const select = label.parentElement.querySelector("select");
        if (!select) throw new Error("Select not found for field: " + labelText);
        return select;
      }
      function setSelect(labelText, value) {
        const select = selectByLabel(labelText);
        select.value = value;
        select.dispatchEvent(new Event("change", { bubbles: true }));
      }
      function requireSelectIncludes(labelText, values) {
        const select = selectByLabel(labelText);
        values.forEach((value) => {
          if (!Array.from(select.options).some((option) => option.value === value)) {
            throw new Error(labelText + " is missing option: " + value);
          }
        });
      }
      function requireSelectExcludes(labelText, values) {
        const select = selectByLabel(labelText);
        values.forEach((value) => {
          if (Array.from(select.options).some((option) => option.value === value)) {
            throw new Error(labelText + " should not include option: " + value);
          }
        });
      }
      function requireSelectDisabled(labelText) {
        if (!selectByLabel(labelText).disabled) throw new Error(labelText + " should be disabled");
      }
      function requireToggleDisabled(labelText) {
        const toggle = toggleByText(labelText);
        if (toggle.style.cursor !== "not-allowed") throw new Error(labelText + " should be disabled");
      }
      function fieldByLabel(labelText) {
        const labels = Array.from(document.querySelectorAll("label"));
        const label = labels.find((item) => item.textContent.trim() === labelText);
        if (!label || !label.parentElement) throw new Error("Field not found: " + labelText);
        return label.parentElement;
      }
      function cardByTitle(title) {
        const card = Array.from(document.querySelectorAll(".card")).find((item) => {
          const heading = item.querySelector("h3");
          return heading && heading.textContent.trim() === title;
        });
        if (!card) throw new Error("Card not found: " + title);
        return card;
      }
      function expandCard(title) {
        const card = cardByTitle(title);
        if (card.classList.contains("collapsed")) {
          const header = card.querySelector(".card-header");
          if (!header) throw new Error("Card header not found: " + title);
          header.click();
        }
        return card;
      }
      function requireSettingsSections() {
        clickTab("Device");
        const expected = [
          ["Display", ["Screen Brightness", "Screen Tone", "Rotation", "Clock"]],
          ["Sleep & Schedule", ["Night Schedule"]],
          ["System", ["Backup", "Firmware", "Device Reboot"]]
        ];
        const sections = Array.from(document.querySelectorAll("#sp-settings .settings-section"));
        const sectionNames = sections.map((section) => {
          const heading = section.querySelector(":scope > .settings-section-title");
          if (!heading || heading.tagName !== "H2" || section.getAttribute("aria-labelledby") !== heading.id) {
            throw new Error("Settings section is missing its accessible heading");
          }
          return heading.textContent.trim();
        });
        if (sectionNames.join("|") !== expected.map((entry) => entry[0]).join("|")) {
          throw new Error("Unexpected settings section order: " + sectionNames.join(", "));
        }
        expected.forEach(([name, expectedCards]) => {
          const section = sections.find((item) => item.querySelector(":scope > .settings-section-title").textContent.trim() === name);
          const cardNames = Array.from(section.querySelectorAll(":scope > .card > .card-header > h3"))
            .map((heading) => heading.textContent.trim());
          if (cardNames.join("|") !== expectedCards.join("|")) {
            throw new Error(name + " cards are not logically ordered: " + cardNames.join(", "));
          }
        });
      }
      function disclosureByTitle(title) {
        const disclosure = Array.from(document.querySelectorAll(".inline-disclosure")).find((item) => {
          const button = item.querySelector(".inline-disclosure-button");
          return button && button.textContent.indexOf(title) !== -1;
        });
        if (!disclosure) throw new Error("Firmware panel not found: " + title);
        return disclosure;
      }
      function expandDisclosure(title) {
        const disclosure = disclosureByTitle(title);
        const button = disclosure.querySelector(".inline-disclosure-button");
        if (button.getAttribute("aria-expanded") !== "true") button.click();
        if (button.getAttribute("aria-expanded") !== "true") throw new Error(title + " did not expand");
        return disclosure;
      }
      function toggleInDisclosure(title, label) {
        const disclosure = expandDisclosure(title);
        const row = Array.from(disclosure.querySelectorAll(".toggle-row")).find((item) =>
          Array.from(item.querySelectorAll("span")).some((span) => span.textContent.trim() === label)
        );
        const toggle = row && row.querySelector(".toggle");
        if (!toggle) throw new Error("Toggle not found in " + title + ": " + label);
        return toggle;
      }
      async function requireFirmwarePanels() {
        const card = expandCard("Firmware");
        await waitFor(() => card.textContent.indexOf("v1.0.1") !== -1, 4000, "firmware version index");
        ["Firmware updates", "Auto updates", "WiFi firmware", "Previous firmware"].forEach((title) => {
          const button = disclosureByTitle(title).querySelector(".inline-disclosure-button");
          if (button.tagName !== "BUTTON" || button.getAttribute("aria-expanded") !== "false") {
            throw new Error(title + " disclosure is not an accessible collapsed button");
          }
        });
        if (!card.querySelector(".badge.active")) throw new Error("Outer firmware update badge is not active");
        const updates = expandDisclosure("Firmware updates");
        if (updates.textContent.indexOf("Current version") === -1 || updates.textContent.indexOf("v1.0.0") === -1) {
          throw new Error("Current firmware version is missing");
        }
        if (updates.textContent.indexOf("Available version") === -1 || updates.textContent.indexOf("v1.0.1") === -1) {
          throw new Error("Available firmware version is missing");
        }
        if (!disclosureByTitle("Firmware updates").querySelector(".disclosure-badge.active")) {
          throw new Error("Main firmware update badge is not active");
        }
        if (!disclosureByTitle("Auto updates").querySelector(".disclosure-badge.active")) {
          throw new Error("Automatic update badge is not active");
        }
        const wifi = expandDisclosure("WiFi firmware");
        if (wifi.textContent.indexOf("2.0.0") === -1 || wifi.textContent.indexOf("2.0.1") === -1) {
          throw new Error("WiFi firmware versions are missing");
        }
        if (!wifi.querySelector(".disclosure-badge.active")) throw new Error("WiFi update badge is not active");
        const previous = expandDisclosure("Previous firmware");
        const versions = Array.from(previous.querySelectorAll("option")).map((option) => option.value);
        if (JSON.stringify(versions) !== JSON.stringify(["v0.9.0"])) {
          throw new Error("Rollback choices are wrong: " + JSON.stringify(versions));
        }
      }
      function setRangeByLabel(labelText, value) {
        const inputEl = fieldByLabel(labelText).querySelector('input[type="range"]');
        if (!inputEl) throw new Error("Range input not found for field: " + labelText);
        inputEl.value = String(value);
        inputEl.dispatchEvent(new Event("input", { bubbles: true }));
        inputEl.dispatchEvent(new Event("change", { bubbles: true }));
      }
      function setCardRange(cardTitle, index, value) {
        const inputs = Array.from(expandCard(cardTitle).querySelectorAll('input[type="range"]'));
        const inputEl = inputs[index];
        if (!inputEl) throw new Error("Range input " + index + " not found in card: " + cardTitle);
        inputEl.value = String(value);
        inputEl.dispatchEvent(new Event("input", { bubbles: true }));
        inputEl.dispatchEvent(new Event("change", { bubbles: true }));
      }
      function inputByLabel(labelText) {
        const inputEl = fieldByLabel(labelText).querySelector("input");
        if (!inputEl) throw new Error("Input not found for field: " + labelText);
        return inputEl;
      }
      function setInputByLabel(labelText, value) {
        const inputEl = inputByLabel(labelText);
        inputEl.value = value;
        inputEl.dispatchEvent(new Event("input", { bubbles: true }));
        inputEl.dispatchEvent(new Event("change", { bubbles: true }));
      }
      function inputByAriaLabel(labelText) {
        const inputEl = document.querySelector('input[aria-label="' + labelText + '"]');
        if (!inputEl) throw new Error("Input not found: " + labelText);
        return inputEl;
      }
      function setInputByAriaLabel(labelText, value) {
        const inputEl = inputByAriaLabel(labelText);
        inputEl.value = value;
        inputEl.dispatchEvent(new Event("input", { bubbles: true }));
        inputEl.dispatchEvent(new Event("change", { bubbles: true }));
      }
      function toggleByText(text) {
        const row = Array.from(document.querySelectorAll(".toggle-row")).find((item) =>
          Array.from(item.querySelectorAll("span")).some((span) => span.textContent.trim() === text)
        );
        if (!row) throw new Error("Toggle not found: " + text);
        const toggle = row.querySelector(".toggle");
        if (!toggle) throw new Error("Toggle control not found: " + text);
        return toggle;
      }
      function photoRows(labelText) {
        return Array.from(fieldByLabel(labelText).querySelectorAll(".photo-id-row"));
      }
      function photoRowValues(labelText) {
        return photoRows(labelText).map((row) =>
          Array.from(row.querySelectorAll("input")).map((inputEl) => inputEl.value)
        );
      }
      function requirePhotoSourceModes() {
        const sourceSelect = selectByLabel("Source");
        ["All Photos", "Favorites", "Album", "Person", "Tag", "Memories"].forEach((mode) => {
          if (!Array.from(sourceSelect.options).some((option) => option.value === mode)) {
            throw new Error("Missing photo source mode: " + mode);
          }
          setSelect("Source", mode);
        });
        requireText("Add an album");
        setSelect("Source", "Person");
        requireText("Add a person");
        setSelect("Source", "Tag");
        requireText("Add a tag");
      }
      async function requireAlbumReorderSave() {
        const startingIds = ${JSON.stringify(smokeAlbumIds)};
        const startingLabels = ${JSON.stringify(smokeAlbumLabels)};
        const expectedIds = startingIds.slice().reverse().join(",");
        const expectedLabels = JSON.stringify(startingLabels.slice().reverse());

        setSelect("Source", "Album");
        await waitFor(() => photoRows("Albums").length === 2, 3000, "album rows");

        const before = photoRowValues("Albums");
        if (JSON.stringify(before.map((row) => row[0])) !== JSON.stringify(startingIds)) {
          throw new Error("Unexpected starting album ID order: " + JSON.stringify(before));
        }
        if (JSON.stringify(before.map((row) => row[1])) !== JSON.stringify(startingLabels)) {
          throw new Error("Unexpected starting album label order: " + JSON.stringify(before));
        }

        const moveUp = photoRows("Albums")[1].querySelector('[aria-label="Move album up"]');
        if (!moveUp || moveUp.disabled) throw new Error("Second album row cannot move up");
        moveUp.click();

        await waitFor(() => {
          const values = photoRowValues("Albums");
          return values[0] && values[0][0] === startingIds[1] && values[0][1] === startingLabels[1];
        }, 3000, "album row visual reorder");

        await waitFor(() => {
          try {
            requireLatestPostValue("Album IDs", "Photos: Album IDs", expectedIds);
            requireLatestPostValue("Album labels", "Photos: Album Labels", expectedLabels);
            return window.__smoke.posts.some((url) => url.indexOf("Apply Photo Source") !== -1);
          } catch (_) {
            return false;
          }
        }, 8000, "album reorder save");
      }
      async function requireScreenRotationDeveloperFlow() {
        clickTab("Device");
        await waitFor(() => pageText().indexOf("Rotation") !== -1, 8000, "device settings");
        requireText("Screen Brightness");
        requireText("Developer");
        requireText("Enable in-development features");

        requireSelectIncludes("Rotation", ["0", "180"]);
        requireSelectExcludes("Rotation", ["90", "270"]);

        toggleByText("Enable in-development features").click();
        await waitFor(() => {
          try {
            requirePostContains("Developer features enable", "Developer: Features", "turn_on");
            requireSelectIncludes("Rotation", ["0", "90", "180", "270"]);
            return true;
          } catch (_) {
            return false;
          }
        }, 8000, "developer rotation options");

        setSelect("Rotation", "90");
        await waitFor(() => {
          try {
            requireLatestPostParam("Portrait rotation", "Screen: Rotation", "option", "90");
            return window.__smoke.posts.some((url) => url.indexOf("Photos: Portrait Pairing") !== -1 && url.indexOf("turn_off") !== -1);
          } catch (_) {
            return false;
          }
        }, 8000, "portrait rotation save");

        toggleByText("Enable in-development features").click();
        await waitFor(() => {
          try {
            requirePostContains("Developer features disable", "Developer: Features", "turn_off");
            requireLatestPostParam("Rotation reset", "Screen: Rotation", "option", "0");
            requireSelectIncludes("Rotation", ["0", "180"]);
            requireSelectExcludes("Rotation", ["90", "270"]);
            return true;
          } catch (_) {
            return false;
          }
        }, 8000, "developer rotation reset");
      }
      async function requireScreenToneScheduleControls() {
        clickTab("Device");
        await waitFor(() => pageText().indexOf("Night Schedule") !== -1, 8000, "device screen controls");
        requireText("Screen Brightness");
        requireText("Screen Tone");
        requireText("Night Schedule");
        requireText("Screen Tone Adjustment");
        requireText("Night Tone Adjustment");
        requireText("Schedule Screen Off");

        expandCard("Screen Brightness");
        setRangeByLabel("Daytime Brightness", 85);
        setRangeByLabel("Nighttime Brightness", 55);

        expandCard("Screen Tone");
        toggleByText("Screen Tone Adjustment").click();
        setCardRange("Screen Tone", 0, 25);
        toggleByText("Night Tone Adjustment").click();
        setCardRange("Screen Tone", 1, 65);
        toggleByText("Turn on until sunrise").click();

        expandCard("Night Schedule");
        toggleByText("Schedule Screen Off").click();
        setSelect("On Time", "7");
        setSelect("Off Time", "21");
        setSelect("When Woken, Idle Time To Screen Off", "120");

        await waitFor(() => {
          try {
            requireLatestPostValue("Daytime brightness", "Screen: Daytime Brightness", "85");
            requireLatestPostValue("Nighttime brightness", "Screen: Nighttime Brightness", "55");
            requirePostContains("Base tone toggle", "Screen: Tone Adjustment", "turn_on");
            requireLatestPostValue("Base tone", "Screen: Display Tone", "25");
            requirePostContains("Night tone toggle", "Screen: Night Tone Adjustment", "turn_on");
            requireLatestPostValue("Night tone intensity", "Screen: Warm Tone Intensity", "65");
            requirePostContains("Warm tone override", "Screen: Warm Tone Override", "turn_on");
            requirePostContains("Schedule toggle", "Screen: Schedule Enabled", "turn_on");
            requireLatestPostValue("Schedule on hour", "Screen: Schedule On Hour", "7");
            requireLatestPostValue("Schedule off hour", "Screen: Schedule Off Hour", "21");
            requireLatestPostValue("Schedule wake timeout", "Screen: Schedule Wake Timeout", "120");
            return true;
          } catch (_) {
            return false;
          }
        }, 8000, "screen tone and schedule saves");
      }
      async function requireDailySettingsControls() {
        expandCard("Connection");
        expandCard("Frequency");
        expandCard("Layout");
        expandCard("Metadata");

        requireText("Connection Timeout");
        requireText("Slideshow Interval");
        requireText("Portrait Pairing");
        requireText("Pairing Range");
        requireText("Paired Portraits Only");
        requireText("Photo Orientation");
        requireText("Display Mode");
        requireText("Metadata");

        setSelect("Connection Timeout", "5 minutes");
        setSelect("Slideshow Interval", "24 hours");
        setSelect("Pairing Range", "Within 2 Days");
        toggleByText("Paired Portraits Only").click();
        toggleByText("Portrait Pairing").click();
        setSelect("Photo Orientation", "Landscape Only");
        setSelect("Display Mode", "Fit");
        setSelect("Date Taken Format", "January 1, 2026");
        setSelect("Date Format", "Relative Date");
        toggleByText("Location").click();
        toggleByText("Date").click();

        clickTab("Device");
        await waitFor(() => pageText().indexOf("Clock") !== -1, 8000, "clock settings");
        clickTab("Immich");
        await waitFor(() => pageText().indexOf("Layout") !== -1, 8000, "pairing disabled state");
        expandCard("Layout");
        requireSelectDisabled("Pairing Range");
        requireToggleDisabled("Paired Portraits Only");
        clickTab("Device");
        await waitFor(() => pageText().indexOf("Clock") !== -1, 8000, "clock settings return");
        expandCard("Clock");
        requireText("Show Clock");
        const advancedClockSettings = disclosureByTitle("Advanced");
        const advancedClockButton = advancedClockSettings.querySelector(".inline-disclosure-button");
        if (advancedClockButton.tagName !== "BUTTON" || advancedClockButton.getAttribute("aria-expanded") !== "false") {
          throw new Error("Clock advanced settings disclosure is not an accessible collapsed button");
        }
        expandDisclosure("Advanced");
        requireText("NTP Servers");

        toggleByText("Show Clock").click();
        setSelect("Format", "12 Hour");
        setSelect("Timezone", "UTC (GMT+0)");
        setInputByAriaLabel("NTP Server 1", "time1.example.com");
        setInputByAriaLabel("NTP Server 2", "time2.example.com");
        setInputByAriaLabel("NTP Server 3", "time3.example.com");

        await waitFor(() => {
          try {
            requireLatestPostParam("Connection timeout", "Screen: Connection Timeout", "option", "5 minutes");
            requireLatestPostParam("Slideshow interval", "Photos: Slideshow Interval", "option", "24 hours");
            requireLatestPostParam("Portrait pairing range", "Photos: Portrait Pairing Range", "option", "Within 2 Days");
            requirePostContains("Paired portraits only", "Photos: Paired Portraits Only", "turn_on");
            requirePostContains("Portrait pairing", "Photos: Portrait Pairing", "turn_off");
            requireLatestPostParam("Photo orientation", "Photos: Orientation", "option", "Landscape Only");
            requireLatestPostParam("Display mode", "Photos: Display Mode", "option", "Fit");
            requireLatestPostParam("Metadata date taken format", "Device: Metadata Date Taken Format", "option", "January 1, 2026");
            requireLatestPostParam("Metadata date format", "Device: Metadata Date Format", "option", "Relative Date");
            requirePostContains("Metadata location toggle", "Device: Metadata Location", "turn_off");
            requirePostContains("Metadata date toggle", "Device: Metadata Date", "turn_off");
            requirePostContains("Show clock toggle", "Clock: Show", "turn_off");
            requireLatestPostParam("Clock format", "Clock: Format", "option", "12 Hour");
            requireLatestPostParam("Timezone", "Clock: Timezone", "option", "UTC (GMT+0)");
            requireLatestPostValue("NTP server 1", "Clock: NTP Server 1", "time1.example.com");
            requireLatestPostValue("NTP server 2", "Clock: NTP Server 2", "time2.example.com");
            requireLatestPostValue("NTP server 3", "Clock: NTP Server 3", "time3.example.com");
            return true;
          } catch (_) {
            return false;
          }
        }, 8000, "daily settings saves");
      }
      async function requireWizardConnectionSave() {
        await waitFor(() => pageText().indexOf("connect your photo frame") !== -1, 8000, "wizard connection step");
        requireText("Immich Server URL");
        requireText("API Key");

        setInputByLabel("Immich Server URL", "setup.photos.example.com/");
        setInputByLabel("API Key", "setup-api-key");
        clickButton("Connect");

        await waitFor(() => pageText().indexOf("Clock & timezone") !== -1, 8000, "wizard clock step");
        requireLatestPostValue("Wizard server URL", "Connection: Server URL", "https://setup.photos.example.com");
        requireLatestPostValue("Wizard API key", "Connection: API Key", "setup-api-key");

        clickButton("Done");
        await waitFor(() => pageText().indexOf("Photo Source") !== -1, 8000, "settings after wizard");
      }

      try {
        if (${JSON.stringify(scenario.name)} === "wizard") {
          await waitFor(() => pageText().indexOf("connect your photo frame") !== -1, 8000, "wizard");
          requireText("Immich Server URL");
          requireText("API Key");
          clickTab("Device");
          requireText("Import Settings");
        } else if (${JSON.stringify(scenario.name)} === "wizard-connection-save") {
          await requireWizardConnectionSave();
        } else {
          await waitFor(() => pageText().indexOf("Photo Source") !== -1, 8000, "settings");
          requireText("Immich Server URL");
          requireText("Photo Source");
          requireText("Date Filter");
          requireText("Fixed Range");
          requireText("Relative Range");
          requireText("Firmware");
          requireText("Auto updates");
          requirePhotoSourceModes();

          if (${JSON.stringify(scenario.name)} === "settings" || ${JSON.stringify(scenario.name)} === "settings-mobile") {
            requireSettingsSections();
            await requireFirmwarePanels();
            toggleInDisclosure("Auto updates", "Auto Update").click();
            toggleInDisclosure("WiFi firmware", "Auto Update").click();
            await waitFor(() => hasConfigurationPost("Firmware: Auto Update") && hasConfigurationPost("WiFi Firmware: Auto Update"), 4000, "firmware automatic update saves");
            if (${JSON.stringify(scenario.name)} === "settings-mobile" && document.documentElement.scrollWidth > window.innerWidth + 4) {
              throw new Error("Grouped settings overflow the mobile viewport");
            }
            clickButton("Export");
            clickButton("Import");
            if (window.__smoke.downloads !== 1) throw new Error("Export did not trigger a download");
            requireExportShape();
            if (window.__smoke.inputClicks !== 1) throw new Error("Import did not open the file picker");
            const logsTab = Array.from(document.querySelectorAll(".sp-tab")).find((tab) => tab.textContent.trim() === "Logs");
            if (!logsTab) throw new Error("Logs tab not found");
            logsTab.click();
            requireText("Clear");
            await waitFor(() => pageText().indexOf("Smoke log line") !== -1, 8000, "log line");
            if (${JSON.stringify(scenario.name)} === "settings-mobile") {
              if (document.documentElement.scrollWidth > window.innerWidth + 4) {
                throw new Error("Mobile viewport has horizontal overflow");
              }
            }
          }

          if (${JSON.stringify(scenario.name)} === "firmware-main-install") {
            await requireFirmwarePanels();
            const install = disclosureByTitle("Firmware updates").querySelector(".fw-actions button");
            install.click();
            install.click();
            await waitFor(() => window.__smoke.posts.some((url) => url.indexOf("Firmware: Update/install") !== -1), 8000, "main firmware install");
            const installPosts = window.__smoke.posts.filter((url) => url.indexOf("Firmware: Update/install") !== -1);
            if (installPosts.length !== 1 || !install.disabled) throw new Error("Main firmware install was not protected against repeated actions");
          }

          if (${JSON.stringify(scenario.name)} === "firmware-c6-install") {
            await requireFirmwarePanels();
            const install = disclosureByTitle("WiFi firmware").querySelector(".fw-actions button");
            install.click();
            install.click();
            await waitFor(() => window.__smoke.posts.some((url) => url.indexOf("Firmware ESP32-C6: Install Update") !== -1), 4000, "WiFi firmware install");
            const installPosts = window.__smoke.posts.filter((url) => url.indexOf("Firmware ESP32-C6: Install Update") !== -1);
            if (installPosts.length !== 1 || !install.disabled) throw new Error("WiFi firmware install was not protected against repeated actions");
          }

          if (${JSON.stringify(scenario.name)} === "firmware-rollback" || ${JSON.stringify(scenario.name)} === "firmware-rollback-failure") {
            await requireFirmwarePanels();
            window.confirm = () => true;
            disclosureByTitle("Previous firmware").querySelector(".fw-previous-actions button").click();
            await waitFor(() => window.__smoke.posts.includes("/update"), 4000, "rollback upload");
            const prepareIndex = window.__smoke.posts.findIndex((url) => url.indexOf("Firmware: Prepare Browser Update/press") !== -1);
            const uploadIndex = window.__smoke.posts.indexOf("/update");
            if (prepareIndex === -1 || prepareIndex > uploadIndex) {
              throw new Error("Rollback upload did not persist reboot recovery state first");
            }
            if (${JSON.stringify(scenario.name)} === "firmware-rollback-failure") {
              await waitFor(() => pageText().indexOf("Device rejected firmware upload (500)") !== -1, 4000, "rollback upload failure");
              const cancelIndex = window.__smoke.posts.findIndex((url) => url.indexOf("Firmware: Cancel Browser Update/press") !== -1);
              if (cancelIndex === -1 || cancelIndex < uploadIndex) {
                throw new Error("Rejected rollback upload did not clear reboot recovery state");
              }
            } else {
              await waitFor(() => pageText().indexOf("Waiting for the display to restart") !== -1, 4000, "rollback reboot wait");
              if (window.__smoke.posts.some((url) => url.indexOf("Firmware: Cancel Browser Update/press") !== -1)) {
                throw new Error("Successful rollback upload unexpectedly cleared reboot recovery state");
              }
            }
          }

          if (${JSON.stringify(scenario.name)} === "firmware-index-unavailable") {
            const card = expandCard("Firmware");
            await new Promise((resolve) => setTimeout(resolve, 200));
            if (Array.from(card.querySelectorAll(".inline-disclosure")).some((item) => item.textContent.indexOf("Previous firmware") !== -1 && item.style.display !== "none")) {
              throw new Error("Previous firmware panel should be hidden when the version index is unavailable");
            }
            const updates = expandDisclosure("Firmware updates");
            if (!Array.from(updates.querySelectorAll("button")).some((button) => button.textContent.trim() === "Check for Update")) {
              throw new Error("Manual firmware check is unavailable without the public version index");
            }
          }

          if (${JSON.stringify(scenario.name)} === "firmware-newer-prerelease") {
            const card = expandCard("Firmware");
            await waitFor(() => card.textContent.indexOf("v1.0.1") !== -1, 4000, "stable firmware version index");
            const updates = expandDisclosure("Firmware updates");
            const action = updates.querySelector(".fw-actions button");
            if (!action || action.textContent.trim() !== "Check for Update") {
              throw new Error("An older stable release was offered as an update to a newer prerelease");
            }
            if (updates.querySelector(".disclosure-badge.active")) {
              throw new Error("An older stable release activated the update badge");
            }
            const previous = expandDisclosure("Previous firmware");
            const stableRollback = Array.from(previous.querySelectorAll("option")).find((option) => option.value === "v1.0.1");
            if (!stableRollback) {
              throw new Error("The latest stable release was missing from prerelease rollback choices");
            }
            action.click();
            await waitFor(() => window.__smoke.posts.some((url) => url.indexOf("Firmware: Check for Update/press") !== -1), 4000, "prerelease update check");
            await new Promise((resolve) => setTimeout(resolve, 4500));
            if (window.__smoke.posts.includes("/update") || window.__smoke.posts.some((url) => url.indexOf("Firmware: Update/install") !== -1)) {
              throw new Error("Checking from a newer prerelease started a downgrade");
            }
          }

          if (${JSON.stringify(scenario.name)} === "firmware-second-device-index") {
            await requireFirmwarePanels();
            if (!window.__smoke.fetchedUrls.some((url) => url.indexOf(${JSON.stringify(expectedFirmwareVersionsPath)}) !== -1)) {
              throw new Error("Firmware history was not loaded from the installed panel's release index");
            }
          }

          if (${JSON.stringify(scenario.name)} === "photo-source-reorder") {
            await requireAlbumReorderSave();
          }

          if (${JSON.stringify(scenario.name)} === "screen-rotation-developer") {
            await requireScreenRotationDeveloperFlow();
          }

          if (${JSON.stringify(scenario.name)} === "screen-tone-schedule") {
            await requireScreenToneScheduleControls();
          }

          if (${JSON.stringify(scenario.name)} === "daily-settings-controls") {
            await requireDailySettingsControls();
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-success") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Settings imported successfully") !== -1, 8000, "successful import");
            if (!hasConfigurationPost("Connection: Server URL")) {
              throw new Error("Import did not post connection URL");
            }
            requirePostContains("Import text field", "Connection: Server URL");
            requirePostContains("Import switch field", "Firmware: Auto Update", "turn_on");
            requirePostContains("Import select field", "Photos: Source", "option=Person");
            requirePostContains("Import number field", "Screen: Daytime Brightness", "value=90");
            requirePostContains("Import aggregate NTP field", "Clock: NTP Server 1");
            requirePostContains("Import WiFi auto-update field", "WiFi Firmware: Auto Update", "turn_on");
            requirePostContains("Import normalized schedule setting", "Screen: Schedule Wake Timeout", "value=120");
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-save-failure") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Imported with 1 failed setting") !== -1, 8000, "failed import save");
            requirePostContains("Failed import still attempted daytime brightness", "Screen: Daytime Brightness", "value=90");
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-partial") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Imported with 1 skipped setting") !== -1, 8000, "partial import");
            requirePostContains("Partial import text field", "Connection: Server URL");
            if (hasConfigurationPost("Photos: Album IDs")) {
              throw new Error("Skipped album IDs were posted to the device");
            }
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-rejected") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Import skipped 1 setting") !== -1, 8000, "rejected import");
            if (hasConfigurationPost("Photos: Album IDs")) {
              throw new Error("Rejected album IDs were posted to the device");
            }
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-missing-version") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Invalid config file - missing version") !== -1, 8000, "missing version rejection");
            if (window.__smoke.posts.length) throw new Error("Missing-version backup wrote settings to the device");
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-future-version") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Unsupported backup version 2 - this device supports version 1") !== -1, 8000, "future version rejection");
            if (window.__smoke.posts.length) throw new Error("Future-version backup wrote settings to the device");
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-unsupported-version") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Unsupported backup version 0") !== -1, 8000, "unsupported version rejection");
            if (window.__smoke.posts.length) throw new Error("Unsupported-version backup wrote settings to the device");
          }
        }
        if (window.__smoke.errors.length) throw new Error(window.__smoke.errors.join("; "));
        document.documentElement.setAttribute("data-smoke-${scenario.name}", "pass");
        document.body.appendChild(document.createTextNode(" ESPFRAME_BROWSER_SMOKE_${scenario.name.toUpperCase().replace(/-/g, "_")}_PASS "));
      } catch (error) {
        document.documentElement.setAttribute("data-smoke-${scenario.name}", "fail");
        const pre = document.createElement("pre");
        pre.id = "smoke-error-${scenario.name}";
        pre.textContent = error && error.stack ? error.stack : String(error);
        document.body.appendChild(pre);
      }
    })();
  `;
}

function htmlForScenario(scenario) {
  const escapedAppSource = appSource.replace(/<\/script/gi, "<\\/script");
  return `<!doctype html>
<html>
<head><meta charset="utf-8"><title>Espframe web smoke ${scenario.name}</title></head>
<body><esp-app></esp-app>
<script>${browserScriptForScenario(scenario)}</script>
<script>${escapedAppSource}</script>
<script>${smokeAssertionsForScenario(scenario)}</script>
</body>
</html>`;
}

function runChrome(args, timeoutMs) {
  return new Promise((resolve) => {
    const useProcessGroup = process.platform !== "win32";
    const child = spawn(requireChromePath(), args, {
      detached: useProcessGroup,
      stdio: ["ignore", "pipe", "pipe"],
    });
    let stdout = "";
    let stderr = "";
    let settled = false;
    let timer = null;
    let forceResolveTimer = null;
    let timedOut = false;

    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    child.stdout.on("data", (chunk) => {
      stdout += chunk;
    });
    child.stderr.on("data", (chunk) => {
      stderr += chunk;
    });

    function finish(result) {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      clearTimeout(forceResolveTimer);
      resolve(result);
    }

    timer = setTimeout(() => {
      timedOut = true;
      stderr += `\nChrome timed out after ${timeoutMs}ms`;
      if (useProcessGroup) {
        try {
          process.kill(-child.pid, "SIGKILL");
        } catch (_) {
          // Fall back to killing the browser wrapper if the process group is already gone.
          child.kill("SIGKILL");
        }
      } else {
        child.kill("SIGKILL");
      }
      forceResolveTimer = setTimeout(() => {
        finish({ status: null, signal: "timeout", stdout, stderr, timedOut });
      }, 1000);
    }, timeoutMs);

    child.on("close", (status, signal) => {
      finish({ status, signal, stdout, stderr, timedOut });
    });
  });
}

async function runScenario(scenario) {
  console.log(`running web browser smoke scenario: ${scenario.name}`);
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "espframe-web-smoke-"));
  const htmlPath = path.join(tempDir, `${scenario.name}.html`);
  const userDataDir = path.join(tempDir, "chrome-profile");
  const query = scenario.query ? `?${scenario.query}` : "";
  fs.writeFileSync(htmlPath, htmlForScenario(scenario));
  const result = await runChrome(
    [
      "--headless=new",
      "--disable-gpu",
      "--disable-background-networking",
      "--disable-component-extensions-with-background-pages",
      "--disable-default-apps",
      "--disable-extensions",
      "--no-first-run",
      "--no-default-browser-check",
      "--no-service-autorun",
      ...chromeSandboxArgs(),
      `--user-data-dir=${userDataDir}`,
      `--window-size=${scenario.width},${scenario.height}`,
      "--virtual-time-budget=16000",
      "--dump-dom",
      `file://${htmlPath}${query}`,
    ],
    30000
  );

  const output = `${result.stdout || ""}\n${result.stderr || ""}`;
  const passToken = `ESPFRAME_BROWSER_SMOKE_${scenario.name.toUpperCase().replace(/-/g, "_")}_PASS`;
  if (!output.includes(passToken)) {
    assert.equal(result.timedOut, false, `Chrome timed out for ${scenario.name}:\n${output}`);
    assert.equal(result.status, 0, `Chrome failed for ${scenario.name} (signal: ${result.signal || "none"}):\n${output}`);
  }
  assert.ok(output.includes(passToken), `Browser smoke scenario ${scenario.name} failed:\n${output}`);
}

function selectedScenariosFromArgs(args) {
  const selectedNames = [];
  for (let i = 0; i < args.length; i += 1) {
    const arg = args[i];
    if (arg === "--list") {
      scenarios.forEach((scenario) => console.log(scenario.name));
      return [];
    }
    if (arg === "--scenario") {
      const value = args[i + 1];
      if (!value) throw new Error("--scenario requires a scenario name");
      selectedNames.push(value);
      i += 1;
      continue;
    }
    if (arg.startsWith("--scenario=")) {
      const value = arg.slice("--scenario=".length);
      if (!value) throw new Error("--scenario requires a scenario name");
      selectedNames.push(value);
      continue;
    }
    throw new Error(`Unknown browser smoke option: ${arg}`);
  }
  if (!selectedNames.length) return scenarios;
  const scenarioByName = new Map(scenarios.map((scenario) => [scenario.name, scenario]));
  return selectedNames.map((name) => {
    const scenario = scenarioByName.get(name);
    if (!scenario) {
      throw new Error(`Unknown browser smoke scenario: ${name}. Run with --list to see available scenarios.`);
    }
    return scenario;
  });
}

async function main(args = process.argv.slice(2)) {
  const selectedScenarios = selectedScenariosFromArgs(args);
  for (const scenario of selectedScenarios) {
    await runScenario(scenario);
  }
  if (selectedScenarios.length) console.log("web browser smoke tests passed");
}

if (require.main === module) {
  main().catch((error) => {
    console.error(error);
    process.exit(1);
  });
}

module.exports = {
  main,
  scenarios,
  selectedScenariosFromArgs,
};
