const assert = require("assert/strict");

const { scenarios, selectedScenariosFromArgs } = require("./web_smoke_tests");

function scenarioNames(selectedScenarios) {
  return selectedScenarios.map((scenario) => scenario.name);
}

function captureScenarioList() {
  const lines = [];
  const originalLog = console.log;
  console.log = (line) => lines.push(String(line));
  try {
    assert.deepEqual(selectedScenariosFromArgs(["--list"]), []);
  } finally {
    console.log = originalLog;
  }
  return lines;
}

const allScenarioNames = scenarioNames(scenarios);

assert.deepEqual(selectedScenariosFromArgs([]), scenarios, "no args should run every smoke scenario");
assert.deepEqual(captureScenarioList(), allScenarioNames, "--list should print every scenario name");
assert.deepEqual(
  scenarioNames(selectedScenariosFromArgs(["--scenario", "wizard-connection-save"])),
  ["wizard-connection-save"],
  "--scenario should select one named scenario"
);
assert.deepEqual(
  scenarioNames(selectedScenariosFromArgs(["--scenario=settings-mobile", "--scenario", "backup-import-success"])),
  ["settings-mobile", "backup-import-success"],
  "--scenario=value and repeated --scenario should preserve selection order"
);
assert.throws(
  () => selectedScenariosFromArgs(["--scenario"]),
  /--scenario requires a scenario name/,
  "--scenario without a value should fail"
);
assert.throws(
  () => selectedScenariosFromArgs(["--scenario="]),
  /--scenario requires a scenario name/,
  "--scenario= without a value should fail"
);
assert.throws(
  () => selectedScenariosFromArgs(["--scenario", "unknown"]),
  /Unknown browser smoke scenario: unknown/,
  "unknown scenarios should fail with a useful message"
);
assert.throws(
  () => selectedScenariosFromArgs(["--unknown"]),
  /Unknown browser smoke option: --unknown/,
  "unknown options should fail"
);

console.log("web smoke cli tests passed");
