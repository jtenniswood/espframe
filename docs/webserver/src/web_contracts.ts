type ConfigurationValue = string | number | boolean;
type ConfigurationValues = Record<string, ConfigurationValue>;

interface ConfigurationSnapshot {
  api_version: number;
  values: ConfigurationValues;
  unavailable: string[];
}

interface ConfigurationUpdateResponse {
  api_version: number;
  status: "accepted" | "rejected";
  updated?: number;
  error?: string;
  field?: string;
}

interface ProductSetting {
  default?: ConfigurationValue;
  domain?: "number" | "select" | "switch" | "text";
  min?: number;
  max?: number;
  step?: number;
  maxLength?: number;
  options?: string[];
  developerOptions?: string[];
  entity?: string;
}

type AppState = Record<string, any>;

class EspframeAppElement extends HTMLElement {}

if (!customElements.get("espframe-app")) {
  customElements.define("espframe-app", EspframeAppElement);
}

function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function parseConfigurationSnapshot(value: unknown): ConfigurationSnapshot | null {
  if (!isObject(value) || value.api_version !== 1 || !isObject(value.values) || !Array.isArray(value.unavailable)) {
    return null;
  }
  var values: ConfigurationValues = {};
  for (var entry of Object.entries(value.values)) {
    var fieldValue = entry[1];
    if (typeof fieldValue !== "string" && typeof fieldValue !== "number" && typeof fieldValue !== "boolean") {
      return null;
    }
    values[entry[0]] = fieldValue;
  }
  if (!value.unavailable.every(function (key): key is string { return typeof key === "string"; })) return null;
  return { api_version: 1, values: values, unavailable: value.unavailable };
}

function configurationUpdateBody(values: ConfigurationValues): string {
  var body = new URLSearchParams();
  body.set("configuration", JSON.stringify({ api_version: 1, values: values }));
  return body.toString();
}
