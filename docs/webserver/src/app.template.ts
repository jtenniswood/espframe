"use strict";

import { SettingSaveCoordinator } from "./setting_save";
import { EspframeApiClient, EspframeApiError, type LegacySettingWrite } from "./api_client";

import {
  MAX_PHOTO_ID_FIELD_LENGTH, extractUrlHost, extractUrlPort, isValidHttpUrl,
  normalizeImmichUrl, normalizeNtpServer, normalizeDateTakenFormat,
  parsePhotoLabelList, photoIdFieldTooLong, photoLabelFieldTooLong,
  splitPhotoIdList, isValidUuidList, formatWireguardHandshakeAge, isValidWireguardKey, isValidWireguardAddress,
  isValidIpv4Netmask, isValidWireguardPort, isValidWireguardEndpoint, normalizeWireguardAllowedIps
} from "./compat";

  __ESPFRAME_WEB_CONTRACTS__

  type AppState = Partial<__ESPFRAME_SETTING_STATE_TYPES__> & RuntimeState;

  var TIMEZONES = __ESPFRAME_TIMEZONES__;
  var TIMEZONE_LABELS = __ESPFRAME_TIMEZONE_LABELS__;
  var PRODUCT_SETTINGS: Record<string, ProductSetting> = __ESPFRAME_PRODUCT_SETTINGS__;
  var STATIC_ENTITIES = __ESPFRAME_STATIC_ENTITIES__;
  var MANUAL_ENTITIES = __ESPFRAME_MANUAL_ENTITIES__;
  var MANUAL_STATE_KEYS = __ESPFRAME_MANUAL_STATE_KEYS__;
  var ENTITY_ALIASES = __ESPFRAME_ENTITY_ALIASES__;
  var BACKUP_CONFIG_VERSION = __ESPFRAME_BACKUP_CONFIG_VERSION__;
  var BACKUP_SCHEMA = __ESPFRAME_BACKUP_SCHEMA__;
  var LIVE_RENDER_STATE_KEYS = __ESPFRAME_LIVE_RENDER_STATE_KEYS__;
  var LIVE_RENDER_STATE_PREFIXES = __ESPFRAME_LIVE_RENDER_STATE_PREFIXES__;
  var FIRMWARE_MANIFEST_URLS = __ESPFRAME_FIRMWARE_MANIFEST_URLS__;
  var FIRMWARE_DEVICE_SLUG = __ESPFRAME_FIRMWARE_DEVICE_SLUG__;
  var DOCS_BASE_URL = __ESPFRAME_DOCS_BASE_URL__;
  var WEB_UI_TABS = __ESPFRAME_WEB_UI_TABS__;
  var WEB_UI_CARDS = __ESPFRAME_WEB_UI_CARDS__;
  var WEB_UI_LOGS_RETAINED_LINES = __ESPFRAME_WEB_UI_LOGS_RETAINED_LINES__;
  var SUPPORT_URL = __ESPFRAME_SUPPORT_URL__;
  var SUPPORT_BUTTON_IMAGE_DATA_URI = __ESPFRAME_SUPPORT_BUTTON_IMAGE_DATA_URI__;
  var CONFIGURATION_CAPABILITIES_PATH: string = __ESPFRAME_CONFIGURATION_CAPABILITIES_PATH__;
  var highlightApiKeyAfterRestore = false;
  var nativeAdmin = document.documentElement.hasAttribute("data-native-admin");
  var nativeSettings = new Set<string>();

  var S: AppState = {
    tz_options: TIMEZONES,
    tz_labels: TIMEZONE_LABELS,
    brightness: 100,
    backlight_on: true,
    immich_url: "",
    api_key_configured: false,
    wireguard_available: false,
    wireguard_private_key_configured: false,
    firmware: "",
    installed_version: "",
    latest_version: "",
    update_available: false,
    firmware_version_options: [],
    firmware_versions_loaded: false,
    firmware_versions_loading: false,
    firmware_selected_version: "",
    firmware_checking: false,
    firmware_installing: false,
    firmware_uploading: false,
    firmware_restart_pending: false,
    firmware_install_error: "",
    c6_firmware_checking: false,
    c6_firmware_installing: false,
    brightness_current: 0,
    sunrise: "",
    sunset: "",
    album_ids: "",
    album_labels: "",
    person_ids: "",
    person_labels: "",
    tag_ids: "",
    tag_labels: "",
    developer_features_enabled: false,
  };

  function registerStaticEntityStateDefaults() {
    if (!STATIC_ENTITIES) return;
    Object.keys(STATIC_ENTITIES).forEach(function (key) {
      var spec = STATIC_ENTITIES[key];
      if (!spec || spec.default === undefined) return;
      if (S[key] === undefined) S[key] = spec.default;
    });
  }

  function registerProductSettingStateDefaults() {
    if (!PRODUCT_SETTINGS) return;
    Object.keys(PRODUCT_SETTINGS).forEach(function (key) {
      var spec = PRODUCT_SETTINGS[key];
      if (!spec) return;
      if (S[key] === undefined) S[key] = spec.default !== undefined ? spec.default : "";
    });
  }

  registerStaticEntityStateDefaults();
  registerProductSettingStateDefaults();

  function productNumberSettingField(key, field, fallback) {
    var spec = PRODUCT_SETTINGS && PRODUCT_SETTINGS[key];
    var value = spec && spec[field] !== undefined ? Number(spec[field]) : NaN;
    return isFinite(value) ? value : fallback;
  }

  function productNumberMin(key, fallback) {
    return productNumberSettingField(key, "min", fallback);
  }

  function productNumberMax(key, fallback) {
    return productNumberSettingField(key, "max", fallback);
  }

  function productNumberStep(key, fallback) {
    return productNumberSettingField(key, "step", fallback);
  }

  function productTextMaxLength(key, fallback) {
    var spec = PRODUCT_SETTINGS && PRODUCT_SETTINGS[key];
    var value = spec && spec.maxLength !== undefined ? Number(spec.maxLength) : NaN;
    return isFinite(value) && value > 0 ? value : fallback;
  }

  function productSettingOptions(key, includeDeveloper?) {
    var spec = PRODUCT_SETTINGS && PRODUCT_SETTINGS[key];
    var options = spec && Array.isArray(spec.options) ? spec.options.slice() : [];
    if (includeDeveloper && spec && Array.isArray(spec.developerOptions)) {
      spec.developerOptions.forEach(function (option) {
        if (options.indexOf(option) === -1) options.push(option);
      });
    }
    return options;
  }

  var CSS = __ESPFRAME_CSS__;
  var FAVICON_SVG = '<svg xmlns="http://www.w3.org/2000/svg" id="mdi-home-automation" viewBox="0 0 24 24"><path fill="#5c73e7" d="M12,3L2,12H5V20H19V12H22L12,3M12,8.5C14.34,8.5 16.46,9.43 18,10.94L16.8,12.12C15.58,10.91 13.88,10.17 12,10.17C10.12,10.17 8.42,10.91 7.2,12.12L6,10.94C7.54,9.43 9.66,8.5 12,8.5M12,11.83C13.4,11.83 14.67,12.39 15.6,13.3L14.4,14.47C13.79,13.87 12.94,13.5 12,13.5C11.06,13.5 10.21,13.87 9.6,14.47L8.4,13.3C9.33,12.39 10.6,11.83 12,11.83M12,15.17C12.94,15.17 13.7,15.91 13.7,16.83C13.7,17.75 12.94,18.5 12,18.5C11.06,18.5 10.3,17.75 10.3,16.83C10.3,15.91 11.06,15.17 12,15.17Z"/></svg>';

  if (!nativeAdmin) {
    var style = document.createElement("style");
    style.textContent = CSS;
    document.head.appendChild(style);
  }
  ensureFavicon();

  var els: Record<string, any> = {};
  var app: HTMLElement;

  __ESPFRAME_WEB_APP_SHELL__

  var apiClient = new EspframeApiClient(CONFIGURATION_CAPABILITIES_PATH);

  __ESPFRAME_WEB_ENDPOINTS__

  // Matches the ESPHome template text max_length for album/person/tag ID and label lists.
  var MAX_NTP_SERVER_LENGTH = 253;
  var PHOTO_ID_FIELD_TOO_LONG =
    "List exceeds 255 characters (device limit). Remove IDs or shorten the list.";
  var PHOTO_LABEL_FIELD_TOO_LONG =
    "Labels exceed 255 characters (device limit). Shorten or remove labels.";

  function delayMs(ms) {
    return new Promise(function (resolve) { setTimeout(resolve, ms); });
  }

  function connectionResponseValue(resp) {
    return (resp && (resp.value || resp.state)) || "";
  }

  function normalizeConnectionUrl(value) {
    var url = normalizeImmichUrl(value);
    return nativeAdmin ? window["espframeNativeUrl"](url) : url;
  }

  function saveAndVerifyConnectionUrl(value) {
    if (nativeAdmin) return saveAndVerifyConnection({ immich_url: value });
    return settingSaves.save({ immich_url: value }, async function () {
      var path = endpoints.immich_url;
      await apiClient.postText(path + "/set", value, true).catch(function (err) {
        console.error("POST " + path + "/set error:", err);
        showBanner("Failed to save setting", "error");
        throw err;
      });
      await delayMs(1200);
      if (normalizeImmichUrl(connectionResponseValue(await safeGet(path))) !== value) throw Error("verify_failed");
    });
  }

  function saveAndVerifyApiKey(value) {
    var apiKey = String(value || "").trim();
    if (!apiKey) return Promise.reject(Error("missing_api_key"));
    return saveAndVerifyConnection({ api_key: apiKey });
  }

  function saveAndVerifyConnection(changes: { immich_url?: string; api_key?: string }) {
    var tracked: Record<string, string | boolean> = {};
    if (changes.immich_url !== undefined) tracked.immich_url = changes.immich_url;
    if (changes.api_key) tracked.api_key_configured = true;
    return settingSaves.save(tracked, async function () {
      var writes = Object.keys(changes).map(function (key) { return legacySettingWrite(key, changes[key]); });
      var savedUrl, keyConfigured;
      try {
        var response = await apiClient.updateSettings(changes, writes);
        await delayMs(150);
        var snapshot = nativeAdmin && response?.restart_required
          ? await window["espframeNativeReconnect"](getConfigurationSnapshot)
          : await getConfigurationSnapshot();
        savedUrl = snapshot.values.immich_url;
        keyConfigured = snapshot.api_key_configured;
      } catch (error) {
        if (nativeAdmin || !(error instanceof EspframeApiError) || error.kind !== "unavailable") throw error;
        var [url, key] = await Promise.all([
          changes.immich_url === undefined ? null : safeGet(endpoints.immich_url),
          changes.api_key ? safeGet(endpoints.api_key) : null
        ]);
        savedUrl = connectionResponseValue(url);
        keyConfigured = key && (key.api_key_configured || connectionResponseValue(key));
      }
      if ((changes.immich_url !== undefined && normalizeImmichUrl(savedUrl) !== changes.immich_url) ||
          (changes.api_key && !keyConfigured)) throw Error("verify_failed");
    });
  }

  var PHOTO_SOURCE_APPLY_SETTING_KEYS = [
    "photo_source",
    "album_order",
    "albums_enabled",
    "people_enabled",
    "tags_enabled",
    "favorites_enabled",
    "rating_enabled",
    "location_enabled",
    "inclusion_matching",
    "album_matching",
    "person_matching",
    "favorite_mode",
    "minimum_rating",
    "filter_country",
    "filter_state",
    "filter_city",
    "album_ids",
    "person_ids",
    "tag_ids",
    "excluded_album_ids",
    "excluded_person_ids",
    "excluded_tag_ids",
    "date_filter_enabled",
    "date_filter_mode",
    "date_from",
    "date_to",
    "relative_amount",
    "relative_unit"
  ];

  function settingUsesPhotoSourceApply(key) {
    return PHOTO_SOURCE_APPLY_SETTING_KEYS.indexOf(key) !== -1;
  }

  var settingSaves = new SettingSaveCoordinator(
    function (key) { return S[key]; },
    function (key, value) { S[key] = value; }
  );

  Object.keys(S).forEach(function (key) {
    var value = S[key];
    if (typeof value === "string" || typeof value === "number" || typeof value === "boolean") {
      settingSaves.receive(key, value);
    }
  });

  function legacySettingWrite(key, savedValue): LegacySettingWrite {
    var domain = settingEntityDomain(key);
    return { key: key, domain: domain, url: endpoints[key], value: savedValue };
  }

  function legacyRotationValues(values, requestedRotation = values.screen_rotation ?? S.screen_rotation) {
    if (values.developer_features_enabled === false && isPortraitScreenRotation(S.screen_rotation)) {
      return Object.assign({}, values, { screen_rotation: "0" });
    }
    return isPortraitScreenRotation(requestedRotation) &&
      ("screen_rotation" in values || "portrait_pairing" in values)
      ? Object.assign({}, values, { portrait_pairing: false })
      : values;
  }

  function saveSettingValues(values, requestedRotation?) {
    var legacyValues = legacyRotationValues(values, requestedRotation);
    var trackedValues = Object.assign({}, values);
    var includesApiKey = Object.prototype.hasOwnProperty.call(trackedValues, "api_key");
    // The API key is write-only and must never enter the browser's settings
    // state, even when a backup restore batches it with other values.
    delete trackedValues.api_key;
    var appliedValues = legacyValues;
    var policyKeys = ["developer_features_enabled", "screen_rotation", "portrait_pairing"];
    var affectsRotationPolicy = policyKeys.some(function (key) { return key in values; });
    return settingSaves.save(trackedValues, async function () {
      var response = await apiClient.updateSettings(values, Object.keys(legacyValues).map(function (key) {
        return legacySettingWrite(key, legacyValues[key]);
      }), legacyValues);
      if (nativeAdmin && response?.restart_required) await window["espframeNativeReconnect"](getConfigurationSnapshot);
      if (response && affectsRotationPolicy) {
        try {
          var snapshot = await apiClient.getConfigurationSnapshot();
          appliedValues = Object.assign({}, legacyValues);
          for (var key of policyKeys) {
            if (key in snapshot.values) appliedValues[key] = snapshot.values[key];
          }
        } catch (_) {
          // The write was accepted. Keep the compatible values if readback fails.
        }
      }
      return response;
    }, function () { return appliedValues; }).then(function (response) {
      if (includesApiKey) settingSaves.receive("api_key_configured", true);
      return response;
    });
  }

  function saveGenericSetting(key, value) {
    if (key === "api_key") return saveAndVerifyApiKey(value);
    if (!hasWritableSetting(key)) return Promise.resolve(null);
    var domain = settingEntityDomain(key);
    var savedValue = value;
    if (domain === "switch") savedValue = !!value;
    if (domain === "number") {
      var numberValue = Number(value);
      if (isFinite(numberValue)) savedValue = numberValue;
    }
    if (domain === "select" || domain === "text") savedValue = value == null ? "" : String(value);
    return saveSettingValues({ [key]: savedValue });
  }

  function hasWritableSetting(key) {
    return nativeAdmin ? nativeSettings.has(key) : !!endpoints[key];
  }

  function saveNtpServer(key, value) {
    return saveSettingValues({ [key]: normalizeNtpServer(value) });
  }

  function saveScheduleWakeTimeoutSetting(key, value) {
    return saveGenericSetting(key, normalizeScheduleWakeTimeout(value));
  }

  function saveScreenRotationSetting(key, value) {
    var rotation = String(value);
    if (screenRotationOptionsForUi().indexOf(rotation) === -1) return Promise.resolve(null);
    return saveSettingValues({ screen_rotation: rotation });
  }

  function reportSettingSaveFailure() {
    if (typeof backupImportInProgress !== "undefined" && backupImportInProgress) return;
    showBanner("Failed to save setting", "error");
    renderSettingsAfterEditing();
  }

  var SETTING_SAVE_ADAPTERS = {
    ntp_server_1: saveNtpServer,
    ntp_server_2: saveNtpServer,
    ntp_server_3: saveNtpServer,
    schedule_wake_timeout: saveScheduleWakeTimeoutSetting,
    screen_rotation: saveScreenRotationSetting
  };

  async function applyPhotoSource() {
    if (!nativeAdmin) await post(endpoints.apply_photo_source + "/press");
  }

  function saveSetting(key, value, options?) {
    var opts = options || {};
    var adapter = SETTING_SAVE_ADAPTERS[key];
    var result = adapter ? adapter(key, value, opts) : saveGenericSetting(key, value);
    if (opts.applyPhotoSource && settingUsesPhotoSourceApply(key)) {
      result = Promise.resolve(result).then(function (saved) {
        return applyPhotoSource().then(function () { return saved; });
      });
    }
    // Event handlers may ignore the result; backup import still receives the
    // original rejecting promise so it can report partial failures accurately.
    result.catch(reportSettingSaveFailure);
    return result;
  }

  function makeConnectionUrlField(value) {
    var f = field("Immich Server URL");
    var urlInput = input("url", value, "http://192.168.0.1:2283");
    f.appendChild(urlInput);
    return { field: f, input: urlInput };
  }

  function makeApiKeyInputGroup(options) {
    var opts = options || {};
    var grp = el("div", "input-group");
    var keyInput = input(opts.type || "text", opts.value || "", opts.placeholder || "Your Immich API key");
    var button = null;
    grp.appendChild(keyInput);
    if (opts.toggleVisibility) {
      button = el("button", "btn btn-secondary");
      button.textContent = "Show";
      button.type = "button";
      button.onclick = function () {
        var isPass = keyInput.type === "password";
        keyInput.type = isPass ? "text" : "password";
        button.textContent = isPass ? "Hide" : "Show";
      };
      grp.appendChild(button);
    } else if (opts.buttonText) {
      button = el("button", opts.buttonClass || "btn btn-primary");
      button.textContent = opts.buttonText;
      button.type = "button";
      if (opts.onButtonClick) {
        button.onclick = function () {
          opts.onButtonClick(keyInput, button);
        };
      }
      grp.appendChild(button);
    }
    return { group: grp, input: keyInput, button: button };
  }

  function makeMaskedApiKeyRow(onChange) {
    var row = el("div", "input-group");
    var mask = el("div");
    var cb = el("button", "btn btn-secondary");
    mask.className = "key-mask";
    mask.textContent = "\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022";
    cb.textContent = "Change";
    cb.type = "button";
    cb.onclick = onChange;
    row.appendChild(mask);
    row.appendChild(cb);
    return row;
  }


  function developerPanelEnabledByUrl() {
    try {
      var params = new URLSearchParams(window.location.search || "");
      return params.get("developer") === "experimental" || params.get("dev") === "experimental";
    } catch (_) {
      return false;
    }
  }

  function isPortraitScreenRotation(value) {
    return value === "90" || value === "270";
  }

  function screenRotationOptionsForUi() {
    var options = productSettingOptions("screen_rotation", S.developer_features_enabled);
    return options.length ? options : ["0", "180"];
  }

  function effectiveScreenRotationForUi() {
    var current = String(S.screen_rotation || "0");
    return screenRotationOptionsForUi().indexOf(current) !== -1 ? current : "0";
  }

  function buildPhotoLabelList(idInputs, labelInputs) {
    var labels = [];
    for (var i = 0; i < idInputs.length; i++) {
      if (idInputs[i].value.trim()) labels.push(labelInputs[i].value.trim());
    }
    while (labels.length && !labels[labels.length - 1]) labels.pop();
    return labels.length ? JSON.stringify(labels) : "";
  }

  function safeGet(url) {
    return apiClient.getJson(url)
      .catch(function () {
        return null;
      });
  }

  function displayVersion(value, fallback) {
    var v = String(value || "").trim();
    if (!v) return fallback || "";
    if (v.toLowerCase() === "dev") return "Dev";
    return v;
  }

  // --- SSE-based init ---

  __ESPFRAME_WEB_RUNTIME_STATE__

  __ESPFRAME_WEB_STARTUP_WIZARD__

  __ESPFRAME_WEB_SETTINGS_IMMICH_CARDS__

  __ESPFRAME_WEB_SETTINGS_SCREEN_CARDS__

  __ESPFRAME_WEB_SETTINGS_FIRMWARE_CARD__

  __ESPFRAME_WEB_SETTINGS_CONTROLS__

  __ESPFRAME_WEB_LIVE_HELPERS__

  __ESPFRAME_WEB_FRAME_IDENTITY__

  __ESPFRAME_WEB_BACKUP_IMPORT__

  // --- Init ---

  buildUI();
  if (nativeAdmin) {
    tryRender();
    startNativeSettingsSync();
  }
  else initSSE();
  loadFrameIdentity();
