const assert = require("assert/strict");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawnSync } = require("child_process");
const { createHash } = require("crypto");
const { gunzipSync, brotliDecompressSync } = require("zlib");

const source = fs.readFileSync(path.join(__dirname, "../components/captive_portal/portal.html"), "utf8");
// Byte-for-byte upstream page parity, allowing only click-navigation prevention.
const upstream = source.replace("function(event){event.preventDefault();", "function(){");
assert.equal(createHash("sha256").update(upstream).digest("hex"),
  "98ff9b7ed031268f7dd6483d90b7f0eed6e4cf689cdb14fe4eb5a59b22d8ad37",
  "Captive portal layout differs from EspControl's pinned ESPHome page");
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
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "espframe-wifi-browser-"));
const names = ["Unifi-Devices", "Guest & \"Family\" café", "<img src=x onerror=alert(1)>"];
try {
  for (const scenario of ["selection", "empty", "offline"]) {
    const mock = `<script>
      window.fetch = function (url) {
        if (url !== "/config.json") throw new Error("Unexpected request");
        ${scenario === "offline" ? 'return Promise.reject(new Error("Offline"));' :
          `return Promise.resolve({ok: true, json: function () { return Promise.resolve(${JSON.stringify({name:"Test frame",aps:scenario === "empty" ? [{}] : [{}, ...names.map(ssid=>({ssid}))]})}); }});`}
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
          ${scenario === "selection" ? `
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
          var form = ssid.form;
          check(document.querySelector("h3").textContent === "WiFi Settings", "Standard form heading changed");
          check(form.querySelector("button").textContent === "Save", "Standard save control changed");
          check(document.querySelector('form[action="/update"]'), "Firmware upload form missing");
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
      `--user-data-dir=${path.join(dir, scenario + "-profile")}`, "--virtual-time-budget=2000", "--dump-dom", `file://${file}`];
    if (process.platform === "linux" && process.getuid() === 0) args.unshift("--no-sandbox");
    const result = spawnSync(chrome, args, {encoding: "utf8", timeout: 20000});
    const rendered = (result.stdout || "").replace(/<script\b[^>]*>[\s\S]*?<\/script>/gi, "");
    assert.equal(result.status, 0, result.stderr || String(result.error));
    assert.ok(rendered.includes("WIFI_BROWSER_PASS"), `${scenario} failed: ${rendered}`);
  }
  console.log("WiFi browser selection, password preservation, SSID escaping and manual-entry tests passed");
} finally {
  fs.rmSync(dir, {recursive: true, force: true});
}
