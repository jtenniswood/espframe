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

if (!chromePath) {
  throw new Error(`Google Chrome or Chromium is required for browser smoke tests. Checked: ${chromePathCandidates.join(", ")}`);
}

function chromeSandboxArgs() {
  if (process.platform !== "linux") return [];
  if (typeof process.getuid !== "function" || process.getuid() !== 0) return [];
  return ["--no-sandbox"];
}

function isLocalChromeAbort(result, output) {
  return process.platform === "darwin" &&
    result &&
    result.status === null &&
    result.signal === "SIGABRT" &&
    !String(output || "").trim();
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
    display_mode: "Fill",
  },
  frequency: {
    interval: "30 seconds",
    conn_timeout: "5 minutes",
  },
  firmware_updates: {
    auto_update: true,
    update_frequency: "Weekly",
    manifest_url: "https://firmware.example.com/manifest.json",
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
  { name: "settings", configured: true, width: 1280, height: 900 },
  { name: "settings-mobile", configured: true, width: 390, height: 900 },
  { name: "backup-import-success", configured: true, width: 1280, height: 900, importFixture: validBackupFixture },
  { name: "backup-import-partial", configured: true, width: 1280, height: 900, importFixture: partialBackupFixture },
  { name: "backup-import-rejected", configured: true, width: 1280, height: 900, importFixture: rejectedBackupFixture },
  { name: "backup-import-missing-version", configured: true, width: 1280, height: 900, importFixture: missingVersionBackupFixture },
  { name: "backup-import-future-version", configured: true, width: 1280, height: 900, importFixture: futureVersionBackupFixture },
  { name: "backup-import-unsupported-version", configured: true, width: 1280, height: 900, importFixture: unsupportedVersionBackupFixture },
];

function browserScriptForScenario(scenario) {
  return `
    window.__smoke = {
      posts: [],
      errors: [],
      downloads: 0,
      exportPayloads: [],
      inputClicks: 0,
      importFixture: ${JSON.stringify(scenario.importFixture || null)}
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
      "Firmware: Version": "v1.0.0",
      "Photos: Source": "Album",
      "Photos: Album IDs": "11111111-1111-4111-8111-111111111111",
      "Photos: Album Labels": "Family",
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
      "Photos: Display Mode": "Fill",
      "Photos: Slideshow Interval": "15 seconds",
      "Screen: Connection Timeout": "10 minutes",
      "Clock: Show": true,
      "Clock: Format": "24 Hour",
      "Clock: Timezone": "Europe/London (GMT+0)",
      "Clock: NTP Server 1": "0.pool.ntp.org",
      "Clock: NTP Server 2": "1.pool.ntp.org",
      "Clock: NTP Server 3": "2.pool.ntp.org",
      "Firmware: Auto Update": true,
      "Firmware: Update Frequency": "Daily",
      "Firmware: Manifest URL": "",
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

    window.fetch = function (url, options) {
      const method = options && options.method ? options.method : "GET";
      const decoded = decodeURIComponent(String(url));
      if (method === "POST") window.__smoke.posts.push(decoded);
      if (decoded.indexOf("Firmware: Update") !== -1) {
        return Promise.resolve({
          ok: true,
          status: 200,
          json: () => Promise.resolve({ value: "v1.0.1", state: "UPDATE AVAILABLE", current_version: "v1.0.0", latest_version: "v1.0.1" })
        });
      }
      let value = "";
      Object.keys(endpointValues).forEach((name) => {
        if (decoded.indexOf(name) !== -1) value = endpointValues[name];
      });
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
        const found = window.__smoke.posts.some((url) =>
          url.indexOf(fragment) !== -1 && (!extraFragment || url.indexOf(extraFragment) !== -1)
        );
        if (!found) throw new Error(label + " was not posted to the device");
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

      try {
        if (${JSON.stringify(scenario.name)} === "wizard") {
          await waitFor(() => pageText().indexOf("connect your photo frame") !== -1, 8000, "wizard");
          requireText("Immich Server URL");
          requireText("API Key");
          clickTab("Device");
          requireText("Import Settings");
        } else {
          await waitFor(() => pageText().indexOf("Photo Source") !== -1, 8000, "settings");
          requireText("Immich Server URL");
          requireText("Photo Source");
          requireText("Date Filter");
          requireText("Fixed Range");
          requireText("Relative Range");
          requireText("Firmware");
          requireText("Installed");
          requireText("Auto updates");
          requirePhotoSourceModes();

          if (${JSON.stringify(scenario.name)} === "settings" || ${JSON.stringify(scenario.name)} === "settings-mobile") {
            clickButton("Export");
            clickButton("Import");
            if (window.__smoke.downloads !== 1) throw new Error("Export did not trigger a download");
            requireExportShape();
            if (window.__smoke.inputClicks !== 1) throw new Error("Import did not open the file picker");
            const checkButton = clickButton("Check for Update");
            await waitFor(() => checkButton.textContent.trim() === "Check for Update", 7000, "firmware check");
            requireText("Stable");
            requireText("v1.0.1");
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

          if (${JSON.stringify(scenario.name)} === "backup-import-success") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Settings imported successfully") !== -1, 8000, "successful import");
            if (!window.__smoke.posts.some((url) => url.indexOf("Connection: Server URL") !== -1)) {
              throw new Error("Import did not post connection URL");
            }
            requirePostContains("Import text field", "Connection: Server URL");
            requirePostContains("Import switch field", "Firmware: Auto Update", "turn_on");
            requirePostContains("Import select field", "Photos: Source", "option=Person");
            requirePostContains("Import number field", "Screen: Daytime Brightness", "value=90");
            requirePostContains("Import aggregate NTP field", "Clock: NTP Server 1");
            requirePostContains("Import URL field", "Firmware: Manifest URL");
            requirePostContains("Import normalized schedule setting", "Screen: Schedule Wake Timeout", "value=120");
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-partial") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Imported with 1 skipped setting") !== -1, 8000, "partial import");
            requirePostContains("Partial import text field", "Connection: Server URL");
            if (window.__smoke.posts.some((url) => url.indexOf("Photos: Album IDs") !== -1)) {
              throw new Error("Skipped album IDs were posted to the device");
            }
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-rejected") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Import skipped 1 setting") !== -1, 8000, "rejected import");
            if (window.__smoke.posts.some((url) => url.indexOf("Photos: Album IDs") !== -1)) {
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
    const child = spawn(chromePath, args, {
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
      `file://${htmlPath}`,
    ],
    30000
  );

  const output = `${result.stdout || ""}\n${result.stderr || ""}`;
  const passToken = `ESPFRAME_BROWSER_SMOKE_${scenario.name.toUpperCase().replace(/-/g, "_")}_PASS`;
  if (!output.includes(passToken)) {
    if (isLocalChromeAbort(result, output)) {
      console.warn(`skipping ${scenario.name}: local Chrome aborted before loading the smoke page`);
      return;
    }
    assert.equal(result.timedOut, false, `Chrome timed out for ${scenario.name}:\n${output}`);
    assert.equal(result.status, 0, `Chrome failed for ${scenario.name} (${result.signal || "no signal"}):\n${output}`);
  }
  assert.ok(output.includes(passToken), `Browser smoke scenario ${scenario.name} failed:\n${output}`);
}

async function main() {
  for (const scenario of scenarios) {
    await runScenario(scenario);
  }
  console.log("web browser smoke tests passed");
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
