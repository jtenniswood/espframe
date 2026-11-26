const assert = require("assert/strict");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawnSync } = require("child_process");
const { createHash } = require("crypto");
const { gunzipSync, brotliDecompressSync } = require("zlib");

const template = fs.readFileSync(path.join(__dirname, "../components/captive_portal/portal.html"), "utf8");
const webStyles = fs.readFileSync(path.join(__dirname, "../docs/webserver/src/style.css"), "utf8");
const source = template.replace("/* __ESPFRAME_WEB_STYLE__ */", webStyles);
// Keep provisioning behavior intact, allowing removal of unused metadata updates.
const script = source.match(/<script\b[^>]*>([\s\S]*?)<\/script>/)[1]
  .replace("document.title=`EspFrame WiFi setup`;", "document.title=t.name,document.getElementById(`mac`).innerText=`MAC Address: `+t.mac,document.getElementById(`h1`).innerText=`WiFi Networks: `+t.name;");
assert.equal(createHash("sha256").update(script).digest("hex"),
  "6e9143fc4bf8cd3c370c03432fc6f94c99ab0380337a9dec0d52372dd034f7d6", "Confirmed captive portal script changed");
assert.ok(source.includes(webStyles), "Portal must embed the shared webserver stylesheet");
assert.ok(!/<(?:script|link)\b[^>]*(?:src|href)=["']?https?:/i.test(source), "Portal must work without internet assets");
const embedded = fs.readFileSync(path.join(__dirname, "../components/captive_portal/captive_index.h"), "utf8");
const arrays = [...embedded.matchAll(/INDEX_GZ\[\] PROGMEM = \{([\s\S]*?)\};/g)]
  .map(match => Buffer.from([...match[1].matchAll(/0x([0-9a-f]{2})/g)].map(byte => parseInt(byte[1], 16))));
assert.equal(arrays.length, 2);
assert.equal(gunzipSync(arrays[0]).toString(), source);
assert.equal(brotliDecompressSync(arrays[1]).toString(), source);
const chrome = [process.env.CHROME_BIN, process.env.CHROME_PATH,
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/usr/bin/google-chrome",
  "/usr/bin/chromium", "/usr/bin/chromium-browser"].find((p) => p && fs.existsSync(p));
assert.ok(chrome, "Chrome or Chromium is required for the WiFi setup browser test");
const savedTemplate = fs.readFileSync(path.join(__dirname, "../components/captive_portal/wifi_saved.html"), "utf8");
const savedSource = savedTemplate.replace("/* __ESPFRAME_WEB_STYLE__ */", webStyles);
const savedHeader = fs.readFileSync(path.join(__dirname, "../components/captive_portal/wifi_saved.h"), "utf8");
const savedArrays = [...savedHeader.matchAll(/WIFI_SAVED_GZ\[\] PROGMEM = \{([\s\S]*?)\};/g)]
  .map(match => Buffer.from([...match[1].matchAll(/0x([0-9a-f]{2})/g)].map(byte => parseInt(byte[1], 16))));
assert.equal(savedArrays.length, 2);
assert.equal(gunzipSync(savedArrays[0]).toString(), savedSource);
assert.equal(brotliDecompressSync(savedArrays[1]).toString(), savedSource);
assert.ok(savedSource.includes(webStyles));
assert.ok(!/<(?:script|link)\b[^>]*(?:src|href)=["']?https?:/i.test(savedSource));
const handler = fs.readFileSync(path.join(__dirname, "../components/captive_portal/captive_portal.cpp"), "utf8")
  .split("void CaptivePortal::handle_wifisave")[1].split("void CaptivePortal::setup")[0];
assert.ok(handler.includes('beginResponse(200, ESPHOME_F("text/html"), WIFI_SAVED_GZ, sizeof(WIFI_SAVED_GZ))'),
  "WiFi save must serve the styled confirmation directly");
assert.ok(handler.includes('ESPHOME_F("Content-Encoding"), ESPHOME_F("gzip")'));
assert.ok(handler.includes('ESPHOME_F("Content-Encoding"), ESPHOME_F("br")'));
assert.ok(handler.includes('ESPHOME_F("Cache-Control")'));
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "espframe-wifi-browser-"));
const names = ["Unifi-Devices", "Guest & \"Family\" café", "<img src=x onerror=alert(1)>"];
try {
  for (const scenario of ["selection", "empty", "offline", "saved"]) {
    const mock = `<script>
      window.fetch = function (url) {
        if (url !== "/config.json") throw new Error("Unexpected request");
        ${scenario === "offline" ? 'return Promise.reject(new Error("Offline"));' :
          `return Promise.resolve({ok: true, json: function () { return Promise.resolve(${JSON.stringify({name:"immich-frame-10inch",mac:"30:ED:A0:E2:F3:6A",aps:scenario === "empty" ? [{}] : [{}, ...names.map(ssid=>({ssid}))]})}); }});`}
      };
    </script>`;
    const assertions = `<script>
      setTimeout(function () {
        try {
          function check(value, message) { if (!value) throw new Error(message); }
          var ssid = document.getElementById("ssid");
          var password = document.getElementById("psk");
          var before = location.href;
          password.value = "typed-password";
          ${(scenario === "selection" || scenario === "saved") ? `
          var buttons = document.querySelectorAll(".network");
          var names = ${JSON.stringify(names)};
          check(buttons.length === names.length, "Expected all scanned networks");
          check(!document.querySelector("#net img"), "SSID must remain text");
          names.forEach(function (name, index) {
            buttons[index].querySelector("a").click();
            check(ssid.value === name, "Selected SSID changed");
            check(password.value === "typed-password", "Password reset after network selection");
            check(location.href === before, "Network selection navigated");
            check(document.activeElement === password, "Password field should receive focus");
          });` : `
          check(!document.querySelector(".network"), "Unexpected scanned networks");
          ssid.value = "Manual network";`}
          var colors = getComputedStyle(document.documentElement);
          check(getComputedStyle(document.body).backgroundColor === "rgb(27, 27, 31)", "Webserver page background missing");
          check(colors.getPropertyValue("--accent").trim() === "#5c73e7", "Webserver accent changed");
          check(getComputedStyle(document.querySelector(".card")).borderRadius === "12px", "Webserver card style missing");
          check(getComputedStyle(ssid).backgroundColor === "rgb(46, 46, 50)", "Webserver input style missing");
          check(ssid.labels.length === 1 && password.labels.length === 1, "WiFi fields need visible labels");
          check(document.documentElement.scrollWidth <= window.innerWidth, "Portal overflows narrow viewport");
          check(getComputedStyle(document.querySelector("aside")).display === ${scenario === "saved" ? '"block"' : '"none"'}, "Connection status visibility changed");
          check(document.title === "EspFrame WiFi setup", "Portal title branding changed");
          check(document.querySelector(".sp-brand-label").textContent === "EspFrame", "Portal header branding changed");
          var form = ssid.form;
          check(!document.querySelector("h1, h2, #mac"), "Removed headings or MAC address returned");
          var heading = document.getElementById("available-networks-heading");
          check(heading && heading.textContent === "Available Networks", "Network list title missing");
          check(document.querySelectorAll("h3").length === 1, "Unexpected extra headings");
          check(heading.nextElementSibling === document.getElementById("net"), "Title must appear above the network list");
          check(document.getElementById("net").getAttribute("aria-labelledby") === heading.id, "Network list needs its title as an accessible label");
          check(getComputedStyle(heading).fontSize === "14px", "Network list title must use the small shared card heading style");
          var visibleText = document.body.innerText;
          ["Connect to WiFi", "Choose your network and enter its password to connect your frame.",
           "WiFi Settings", "WiFi Networks", "immich-frame-10inch", "MAC Address", "30:ED:A0:E2:F3:6A"]
            .forEach(function (text) { check(!visibleText.includes(text), "Removed text returned: " + text); });
          check(form.querySelector("button").textContent === "Save", "Standard save control changed");
          check(!document.querySelector('form[action="/update"], input[type="file"]'), "Removed firmware upload controls returned");
          check(document.querySelectorAll("form").length === 1, "Expected only WiFi setup form");
          check(getComputedStyle(form.querySelector("button")).backgroundColor === "rgb(92, 115, 231)", "Webserver button style missing");
          check(form.getAttribute("action") === "/wifisave", "Existing provisioning endpoint changed");
          var submitted = false;
          form.addEventListener("submit", function (event) {
            event.preventDefault();
            var values = new FormData(form);
            check(values.get("ssid") === ssid.value, "Submitted SSID changed");
            check(values.get("psk") === "typed-password", "Submitted password changed");
            submitted = true;
          });
          form.requestSubmit();
          check(submitted, "Connection form failed to submit");
          var result = document.createElement("p");
          result.textContent = "WIFI_BROWSER_PASS";
          document.body.appendChild(result);
        } catch (error) { document.body.textContent = "WIFI_BROWSER_FAIL " + error.message; }
      }, 50);
    </script>`;
    const file = path.join(dir, `${scenario}.html`);
    fs.writeFileSync(file, source.replace(/<script\b/, mock + "<script").replace("</body>", assertions + "</body>"));
    const args = ["--headless=new", "--disable-gpu", "--disable-background-networking", "--no-first-run",
      `--user-data-dir=${path.join(dir, scenario + "-profile")}`, "--virtual-time-budget=2000", "--window-size=360,900", "--dump-dom", `file://${file}${scenario === "saved" ? "?save" : ""}`];
    if (process.platform === "linux" && process.getuid() === 0) args.unshift("--no-sandbox");
    const result = spawnSync(chrome, args, {encoding: "utf8", timeout: 20000});
    const rendered = (result.stdout || "").replace(/<script\b[^>]*>[\s\S]*?<\/script>/gi, "");
    assert.equal(result.status, 0, result.stderr || String(result.error));
    assert.ok(rendered.includes("WIFI_BROWSER_PASS"), `${scenario} failed: ${rendered}`);
  }
  const savedAssertions = `<script>
    try {
      function check(value, message) { if (!value) throw new Error(message); }
      check(document.title === "EspFrame WiFi setup", "Confirmation title branding changed");
      check(document.querySelector(".sp-brand-label").textContent === "EspFrame", "Confirmation header branding changed");
      check(document.querySelector("h1").textContent === "WiFi details saved", "Saved heading missing");
      check(!document.querySelector(".saved-description") && !document.body.innerText.includes("Your WiFi details are saved to your device."), "Saved confirmation must use just one line");
      var steps = document.querySelectorAll(".setup-steps li");
      check(steps.length === 2, "Expected two next steps");
      check(steps[0].textContent === "Reconnect your phone or computer to your home WiFi.", "Reconnect instructions missing");
      check(steps[1].textContent === "Follow the instructions on your frame’s screen to continue setup.", "Screen instructions missing");
      check(getComputedStyle(document.body).backgroundColor === "rgb(27, 27, 31)", "Confirmation background mismatch");
      check(getComputedStyle(document.querySelector(".card")).borderRadius === "12px", "Confirmation card mismatch");
      check(document.documentElement.scrollWidth <= window.innerWidth, "Confirmation overflows narrow viewport");
      check(!document.querySelector("form, input, button"), "Confirmation should need no more input");
      check(document.querySelectorAll("script").length === 1, "Confirmation must work without scripts or further requests");
      var result = document.createElement("p"); result.textContent = "WIFI_SAVED_PASS"; document.body.appendChild(result);
    } catch (error) { document.body.textContent = "WIFI_SAVED_FAIL " + error.message; }
  </script>`;
  const savedFile = path.join(dir, "confirmation.html");
  fs.writeFileSync(savedFile, savedSource.replace("</body>", savedAssertions + "</body>"));
  const savedArgs = ["--headless=new", "--disable-gpu", "--disable-background-networking", "--no-first-run",
    `--user-data-dir=${path.join(dir, "confirmation-profile")}`, "--window-size=360,900", "--dump-dom", `file://${savedFile}`];
  if (process.platform === "linux" && process.getuid() === 0) savedArgs.unshift("--no-sandbox");
  const savedResult = spawnSync(chrome, savedArgs, {encoding: "utf8", timeout: 20000});
  const savedRendered = (savedResult.stdout || "").replace(/<script\b[^>]*>[\s\S]*?<\/script>/gi, "");
  assert.equal(savedResult.status, 0, savedResult.stderr || String(savedResult.error));
  assert.ok(savedRendered.includes("WIFI_SAVED_PASS"), `Confirmation failed: ${savedRendered}`);
  const nativeDir = path.join(__dirname, "../firmware/native/main/web");
  const nativePortal = fs.readFileSync(path.join(nativeDir, "recovery.html"), "utf8");
  const nativeScript = fs.readFileSync(path.join(nativeDir, "recovery.js"), "utf8");
  const nativeStyles = fs.readFileSync(path.join(nativeDir, "recovery.css"), "utf8");
  for (const scanAvailable of [true, false]) {
    const mock = `<script>
      const names = ${JSON.stringify(names)};
      const scanAvailable = ${scanAvailable};
      let scans = 0;
      let sent;
      window.fetch = async (url, options) => {
        if (url === "/api/recovery/networks?details=1") {
          if (!scanAvailable) return new Response(null, {status: 404});
          if (++scans === 1) return new Response(null, {status: 202});
          return Response.json(names.map((ssid, index) => ({ssid, rssi: -50 - index * 10, secured: index !== 0})));
        }
        if (url !== "/api/recovery") throw new Error("Unexpected request");
        if (options.method === "POST") {
          sent = Array.from(options.body);
          return new Response(null, {status: 202});
        }
        return Response.json(sent ? {status: "connected", ipv4: "192.168.1.42"} : {status: "ready"});
      };
    </script>`;
    const assertions = `<script type="module">
      setTimeout(async () => {
        try {
          function check(value, message) { if (!value) throw new Error(message); }
          const form = document.querySelector("#recovery");
          const rows = document.querySelectorAll("#networks .network");
          const {ssid, password} = form.elements;
          password.value = "typed-password";
          check(getComputedStyle(document.body).backgroundColor === "rgb(27, 27, 31)", "Legacy background missing");
          check(getComputedStyle(document.querySelector("#setup")).borderRadius === "12px", "Legacy card styling missing");
          check(getComputedStyle(ssid).backgroundColor === "rgb(46, 46, 50)", "Legacy SSID styling missing");
          check(getComputedStyle(form.querySelector("button")).backgroundColor === "rgb(92, 115, 231)", "Legacy button styling missing");
          check(document.querySelector(".sp-brand-label").textContent === "EspFrame", "Legacy branding missing");
          check(document.documentElement.scrollWidth <= window.innerWidth, "Native portal overflows narrow viewport");
          check(document.querySelector("#nearby").hidden === !scanAvailable, "Incorrect network chooser visibility");
          if (scanAvailable) {
            check(scans === 2, "Should retry a pending scan");
            check(rows.length === names.length, "Missing scan results");
            check(!rows[2].querySelector("img"), "Network name was parsed as markup");
            check(!rows[0].querySelector(".network-lock") && rows[1].querySelector(".network-lock"), "Network security indicator missing");
            check(rows[1].querySelector(".network-signal").getAttribute("transform") === "scale(0.8)", "Signal indicator is incorrect");
            rows[1].querySelector("button").click();
            check(ssid.value === names[1] && document.activeElement === password, "Network selection did not fill the form");
            check(password.value === "typed-password", "Selection reset the typed password");
          } else {
            ssid.value = "Manual network";
          }
          const encoder = new TextEncoder();
          const name = encoder.encode(ssid.value);
          const secret = encoder.encode(password.value);
          form.requestSubmit();
          await new Promise(resolve => setTimeout(resolve, 50));
          check(String(sent) === String([name.length, ...name, secret.length, ...secret]), "Unexpected provisioning body");
          check(form.hidden && !document.querySelector("#saved").hidden, "Saved confirmation missing");
          check(document.querySelector("#saved-heading").textContent === "WiFi details saved", "Legacy confirmation heading missing");
          check(document.querySelector("#continue-setup").textContent.includes("192.168.1.42"), "Missing connected address");
          document.body.append("NATIVE_WIFI_PASS");
        } catch (error) { document.body.textContent = "NATIVE_WIFI_FAIL " + error.message; }
      }, 1000);
    </script>`;
    const scenario = scanAvailable ? "native-selection" : "native-manual";
    const file = path.join(dir, `${scenario}.html`);
    const page = nativePortal.replace('<link rel="stylesheet" href="/espframe/style.css">', `<style>${webStyles}</style>`)
      .replace('<link rel="stylesheet" href="/recovery.css">', `<style>${nativeStyles}</style>`)
      .replace('<script type="module" src="/recovery.js"></script>',
      `${mock}<script type="module">${nativeScript}</script>${assertions}`);
    fs.writeFileSync(file, page);
    const args = ["--headless=new", "--disable-gpu", "--disable-background-networking", "--no-first-run",
      `--user-data-dir=${path.join(dir, scenario + "-profile")}`, "--virtual-time-budget=2000", "--window-size=360,900", "--dump-dom", `file://${file}`];
    if (process.platform === "linux" && process.getuid() === 0) args.unshift("--no-sandbox");
    const result = spawnSync(chrome, args, {encoding: "utf8", timeout: 20000});
    const rendered = (result.stdout || "").replace(/<script\b[^>]*>[\s\S]*?<\/script>/gi, "");
    assert.equal(result.status, 0, result.stderr || String(result.error));
    assert.ok(rendered.includes("NATIVE_WIFI_PASS"), `${scenario} failed: ${rendered}`);
  }
  console.log("Native Wi-Fi setup tests passed for scanned and manually entered networks");
  console.log("WiFi browser shared styling, narrow layout, saved confirmation instructions, branding, connection status, selection, password preservation, SSID escaping and manual-entry tests passed");
} finally {
  fs.rmSync(dir, {recursive: true, force: true});
}
