const assert = require("assert/strict");
const vm = require("vm");
const fs = require("fs");
const { transformSync } = require("esbuild");

const source = fs.readFileSync("docs/webserver/src/api_client.ts", "utf8");
const context = { module: { exports: {} }, exports: {}, fetch, AbortController, URLSearchParams, setTimeout, clearTimeout, Promise };
vm.runInNewContext(transformSync(source, { loader: "ts", format: "cjs", target: "es2018" }).code, context);
const { EspframeApiClient, EspframeApiError } = context.module.exports;
const contract = {
  contract_version: 2, api_version: 1, base_path: "/espframe/api/v1",
  capabilities_path: "/espframe/api/v1/capabilities", configuration_path: "/espframe/api/v1/configuration",
  update_mode: "atomic", configuration_available: true, configuration_read: true,
  configuration_write: true, configuration_encoding: "application/x-www-form-urlencoded",
  configuration_parameter: "configuration", legacy_entity_api: true, backup_versions: [1], setting_count: 1,
};

function response(body, status = 200) {
  return { ok: status >= 200 && status < 300, status, async json() { return body; } };
}

async function main() {
  const calls = [];
  const fakeFetch = async (url, init) => {
    calls.push([url, init && init.method]);
    if (url.endsWith("/capabilities")) return response(contract);
    if (url.endsWith("/configuration") && init && init.method === "POST") return response({ api_version: 1, status: "accepted" });
    if (url.endsWith("/configuration")) return response({ api_version: 1, values: { amount: 2 }, unavailable: [] });
    return response({});
  };
  context.fetch = fakeFetch;
  const client = new EspframeApiClient(contract, 1000);
  await Promise.all([
    client.updateSettings({ amount: 2 }, [{ key: "amount", domain: "number", url: "/number/Amount", value: 2 }]),
    client.updateSettings({ amount: 3 }, [{ key: "amount", domain: "number", url: "/number/Amount", value: 3 }]),
  ]);
  assert.deepEqual(calls.map(call => call[0]), [
    "/espframe/api/v1/capabilities", "/espframe/api/v1/configuration", "/espframe/api/v1/configuration",
  ], "versioned writes should negotiate once and serialize");

  const offline = new EspframeApiClient(contract, 1000);
  context.fetch = async () => { throw new TypeError("network"); };
  await assert.rejects(offline.getConfigurationSnapshot(), error => error instanceof EspframeApiError && error.kind === "offline");
  context.fetch = fakeFetch;
  console.log("API client tests passed");
}
main().catch(error => { console.error(error); process.exitCode = 1; });
