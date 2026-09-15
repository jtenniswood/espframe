export type ConfigurationValue = string | number | boolean;
export type ConfigurationValues = Record<string, ConfigurationValue>;

export interface ConfigurationSnapshot {
  api_version: number;
  api_key_configured: boolean;
  values: ConfigurationValues;
  unavailable: string[];
}

export interface ConfigurationUpdateResponse {
  api_version: number;
  status: "accepted" | "rejected";
  updated?: number;
  error?: string;
  field?: string;
}

export interface ConfigurationCapabilities {
  contract_version: number;
  api_version: number;
  base_path: string;
  capabilities_path: string;
  configuration_path: string;
  update_mode: "atomic";
  configuration_available: boolean;
  configuration_read: boolean;
  configuration_write: boolean;
  configuration_encoding?: string;
  configuration_parameter?: string;
  legacy_entity_api: boolean;
  backup_versions: number[];
  setting_count?: number;
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

interface RuntimeState {
  tz_options: string[];
  tz_labels: Record<string, string>;
  brightness: number;
  brightness_current: number;
  backlight_on: boolean;
  api_key_configured: boolean;
  installed_version: string;
  latest_version: string;
  update_available: boolean;
  firmware_version_options: FirmwareVersionInfo[];
  firmware_versions_loaded: boolean;
  firmware_versions_loading: boolean;
  firmware_selected_version: string;
  firmware_checking: boolean;
  firmware_installing: boolean;
  firmware_uploading: boolean;
  firmware_restart_pending: boolean;
  firmware_install_error: string;
  c6_firmware_checking: boolean;
  c6_firmware_installing: boolean;
}

interface FirmwareVersionInfo {
  version: string;
  release_url: string;
  ota_url: string;
  ota_filename: string;
  ota_md5: string;
}


class EspframeAppElement extends HTMLElement {}

if (!customElements.get("espframe-app")) {
  customElements.define("espframe-app", EspframeAppElement);
}

function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function parseConfigurationSnapshot(value: unknown): ConfigurationSnapshot | null {
  if (!isObject(value) || value.api_version !== 1 ||
      !isObject(value.values) || !Array.isArray(value.unavailable)) {
    return null;
  }
  var apiKeyConfigured = value.api_key_configured as boolean;
  var values: ConfigurationValues = {};
  for (var entry of Object.entries(value.values)) {
    var key = entry[0], fieldValue = entry[1];
    if (key === "api_key") {
      if (apiKeyConfigured != null) return null;
      apiKeyConfigured = !!fieldValue;
      continue;
    }
    if (typeof fieldValue !== "string" && typeof fieldValue !== "number" && typeof fieldValue !== "boolean") {
      return null;
    }
    values[key] = fieldValue;
  }
  if (!value.unavailable.every(key => typeof key === "string")) return null;
  return { api_version: 1, api_key_configured: apiKeyConfigured, values: values, unavailable: value.unavailable };
}

function configurationUpdateBody(values: ConfigurationValues): string {
  var body = new URLSearchParams();
  body.set("configuration", JSON.stringify({ api_version: 1, values: values }));
  return body.toString();
}

interface ConfigurationError extends Error {
  configurationApiUnavailable?: boolean;
  legacy?: boolean;
  configurationApiResponse?: boolean;
  field?: string;
}

interface EntityStateSpec {
  key: string;
  default?: ConfigurationValue;
  optionsKey?: string;
  boolFromState?: boolean;
  number?: boolean;
}
