#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import Ajv2020 from "ajv/dist/2020.js";
import addFormats from "ajv-formats";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const contractDir = path.join(root, "product", "contract");

function readJson(filename) {
  return JSON.parse(fs.readFileSync(path.join(contractDir, filename), "utf8"));
}

const schema = readJson("schema.json");
const contract = {
  manifest: readJson("manifest.json"),
  project: readJson("project.json"),
  devices: readJson("devices.json"),
  settings: readJson("settings.json"),
};

const ajv = new Ajv2020({ allErrors: true, strict: true });
addFormats(ajv);
const validate = ajv.compile(schema);

if (!validate(contract)) {
  for (const error of validate.errors || []) {
    const location = error.instancePath || "/";
    console.error(`product schema error at ${location}: ${error.message}`);
  }
  process.exit(1);
}

const settingKeys = contract.settings.map((setting) => setting.key);
if (new Set(settingKeys).size !== settingKeys.length) {
  console.error("product schema error: setting keys must be unique");
  process.exit(1);
}

const deviceSlugs = contract.devices.map((device) => device.slug);
if (new Set(deviceSlugs).size !== deviceSlugs.length) {
  console.error("product schema error: device slugs must be unique");
  process.exit(1);
}

if (!contract.manifest.compatibility.backup_versions.includes(contract.project.backup_config_version)) {
  console.error("product schema error: current backup version is missing from compatibility.backup_versions");
  process.exit(1);
}

console.log("product contract schema validation passed");
