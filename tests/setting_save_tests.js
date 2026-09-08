const assert = require("assert/strict");
const vm = require("vm");
const fs = require("fs");
const { transformSync } = require("esbuild");
const source = fs.readFileSync("docs/webserver/src/setting_save.ts", "utf8");
const context = { module: { exports: {} } };
vm.runInNewContext(transformSync(source, { loader: "ts", format: "cjs", target: "es2018" }).code, context);
const { SettingSaveCoordinator } = context.module.exports;
const runtimeSource = fs.readFileSync("docs/webserver/src/runtime_state.ts", "utf8");

async function legacyConnectionRollback(status) {
  const state = { immich_url: "", api_key: "" };
  const saves = new SettingSaveCoordinator(key => state[key], (key, value) => { state[key] = value; });
  Object.entries(state).forEach(([key, value]) => saves.receive(key, value));
  let settingsShown;
  const ready = new Promise(resolve => { settingsShown = resolve; });
  let backgroundFetches = 0;
  const runtime = {
    S: state,
    settingSaves: saves,
    rendered: false,
    renderAttemptInFlight: false,
    renderTimer: null,
    setTimeout,
    endpoints: { immich_url: "url", api_key: "key" },
    renderSettings: () => settingsShown(),
    fetchDeviceSettingsState: async () => {
      backgroundFetches++;
      state.immich_url = "https://existing.example.test";
      state.api_key = "existing-key";
      saves.receive("immich_url", state.immich_url);
      saves.receive("api_key", state.api_key);
    },
  };
  vm.runInNewContext(transformSync(runtimeSource.slice(
    runtimeSource.indexOf("  function withStartupTimeout"),
    runtimeSource.indexOf("  function initSSE()")
  ), { loader: "ts" }).code, runtime);
  runtime.tryRender();
  await ready;
  assert.equal(backgroundFetches, 1);
  assert.deepEqual(state, { immich_url: "https://existing.example.test", api_key: "existing-key" });
  await assert.rejects(saves.save({ immich_url: "https://edited.example.test", api_key: "edited-key" },
    async () => { throw new Error("connection save rejected"); }));
  assert.deepEqual(state, { immich_url: "https://existing.example.test", api_key: "existing-key" },
    "failed edits before legacy hydration must restore the device credentials");
}

function deferredFailureRender() {
  const first = new EventTarget();
  const second = new EventTarget();
  first.matches = second.matches = () => true;
  const timers = [];
  let renders = 0;
  const banners = [];
  const runtime = {
    document: { activeElement: first },
    els: { root: { contains: control => [first, second].includes(control) } },
    renderTimer: null,
    setTimeout(callback) { timers.push(callback); return timers.length; },
    renderSettings() { renders++; },
    showBanner: (message, kind) => banners.push([message, kind]),
  };
  const template = fs.readFileSync("docs/webserver/src/app.template.ts", "utf8");
  const functions = runtimeSource.slice(runtimeSource.indexOf("  function isEditingSetting()"),
    runtimeSource.indexOf("  function scheduleTryRender")) +
    template.slice(template.indexOf("  function reportSettingSaveFailure()"),
      template.indexOf("  var SETTING_SAVE_ADAPTERS"));
  vm.runInNewContext(transformSync(functions, { loader: "ts" }).code, runtime);
  runtime.reportSettingSaveFailure();
  runtime.reportSettingSaveFailure();
  assert.deepEqual(banners[0], ["Failed to save setting", "error"]);
  assert.equal(timers.length, 0, "a focused failed control must not start polling");
  assert.equal(renders, 0, "do not replace a control while it is being edited");
  runtime.document.activeElement = second;
  first.dispatchEvent(new Event("blur"));
  assert.equal(timers.length, 1, "repeated failures should share one deferred render");
  timers.shift()();
  assert.equal(timers.length, 0, "moving to another control must wait for blur without polling");
  assert.equal(renders, 0);
  runtime.document.activeElement = null;
  second.dispatchEvent(new Event("blur"));
  assert.equal(timers.length, 1);
  timers.shift()();
  assert.equal(renders, 1, "render the rolled-back state when editing finishes");
  assert.equal(timers.length, 0);
  runtime.reportSettingSaveFailure();
  assert.equal(renders, 2, "unfocused failures should render immediately");
}

async function overlapping(firstFails, secondFails) {
  const state = { amount: 1 };
  const saves = new SettingSaveCoordinator(key => state[key], (key, value) => { state[key] = value; });
  const sent = [];
  let releaseFirst;
  const first = saves.save({ amount: 2 }, () => {
    sent.push(2);
    return new Promise((resolve, reject) => { releaseFirst = () => firstFails ? reject(new Error("first")) : resolve(2); });
  });
  const second = saves.save({ amount: 3 }, async () => {
    sent.push(3);
    if (secondFails) throw new Error("second");
    return 3;
  });
  const results = Promise.allSettled([first, second]);
  await Promise.resolve();
  assert.deepEqual(sent, [2], "second complete write must wait, including any legacy fallback");
  assert.equal(state.amount, 3);
  saves.receive("amount", 1);
  assert.equal(state.amount, 3, "stale live state must not erase a pending draft");
  releaseFirst();
  await results;
  assert.deepEqual(sent, [2, 3]);
  assert.equal(state.amount, secondFails ? (firstFails ? 1 : 2) : 3);
  saves.receive("amount", 4);
  assert.equal(state.amount, 4, "external changes must resume after writes settle");
}

async function main() {
  for (const status of [404, 405]) await legacyConnectionRollback(status);
  deferredFailureRender();
  for (const firstFails of [true, false]) {
    for (const secondFails of [true, false]) await overlapping(firstFails, secondFails);
  }
  const state = { rotation: "0", pairing: true };
  const saves = new SettingSaveCoordinator(key => state[key], (key, value) => { state[key] = value; });
  saves.receive("rotation", "0");
  saves.receive("pairing", true);
  state.pairing = false; // A toggle updates its draft before submitting it.
  await assert.rejects(saves.save({ rotation: "90", pairing: false }, async () => { throw new Error("rejected"); }));
  assert.deepEqual(state, { rotation: "0", pairing: true });
  await saves.save({ rotation: "180", pairing: true }, async () => ({ status: "accepted" }));
  assert.deepEqual(state, { rotation: "180", pairing: true });
  console.log("setting save tests passed");
}
main().catch(error => { console.error(error); process.exitCode = 1; });
