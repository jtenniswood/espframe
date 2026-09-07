const assert = require("assert/strict");
const vm = require("vm");
const fs = require("fs");
const { transformSync } = require("esbuild");
const source = fs.readFileSync("docs/webserver/src/setting_save.ts", "utf8");
const context = { module: { exports: {} } };
vm.runInNewContext(transformSync(source, { loader: "ts", format: "cjs", target: "es2018" }).code, context);
const { SettingSaveCoordinator } = context.module.exports;

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
