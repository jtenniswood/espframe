(function () {
  "use strict";

  var TIMEZONES = __ESPFRAME_TIMEZONES__;
  var TIMEZONE_LABELS = __ESPFRAME_TIMEZONE_LABELS__;
  var PRODUCT_SETTINGS = __ESPFRAME_PRODUCT_SETTINGS__;
  var STATIC_ENTITIES = __ESPFRAME_STATIC_ENTITIES__;
  var MANUAL_ENTITIES = __ESPFRAME_MANUAL_ENTITIES__;
  var MANUAL_STATE_KEYS = __ESPFRAME_MANUAL_STATE_KEYS__;
  var ENTITY_ALIASES = __ESPFRAME_ENTITY_ALIASES__;
  var BACKUP_CONFIG_VERSION = __ESPFRAME_BACKUP_CONFIG_VERSION__;
  var BACKUP_SCHEMA = __ESPFRAME_BACKUP_SCHEMA__;
  var LIVE_RENDER_STATE_KEYS = __ESPFRAME_LIVE_RENDER_STATE_KEYS__;
  var LIVE_RENDER_STATE_PREFIXES = __ESPFRAME_LIVE_RENDER_STATE_PREFIXES__;
  var FIRMWARE_MANIFEST_URLS = __ESPFRAME_FIRMWARE_MANIFEST_URLS__;
  var DOCS_BASE_URL = __ESPFRAME_DOCS_BASE_URL__;
  var WEB_UI_TABS = __ESPFRAME_WEB_UI_TABS__;
  var WEB_UI_LOGS_RETAINED_LINES = __ESPFRAME_WEB_UI_LOGS_RETAINED_LINES__;
  var SUPPORT_URL = __ESPFRAME_SUPPORT_URL__;
  var SUPPORT_BUTTON_IMAGE_URL = __ESPFRAME_SUPPORT_BUTTON_IMAGE_URL__;

  var S = {
    tz_options: TIMEZONES,
    tz_labels: TIMEZONE_LABELS,
    brightness: 100,
    backlight_on: true,
    immich_url: "",
    api_key: "",
    firmware: "",
    installed_version: "",
    latest_version: "",
    update_available: false,
    beta_version: "",
    beta_available: false,
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

  function productSettingOptions(key, includeDeveloper) {
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

  var style = document.createElement("style");
  style.textContent = CSS;
  document.head.appendChild(style);
  ensureFavicon();

  var fonts = document.createElement("link");
  fonts.rel = "stylesheet";
  fonts.href = "https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap";
  document.head.appendChild(fonts);

  var els = {};
  var app;

  __ESPFRAME_WEB_APP_SHELL__

  __ESPFRAME_WEB_ENDPOINTS__

  // Matches the ESPHome template text max_length for album/person/tag ID and label lists.
  var MAX_PHOTO_ID_FIELD_LENGTH = 255;
  var MAX_NTP_SERVER_LENGTH = 253;
  var MAX_FIRMWARE_URL_LENGTH = 255;
  var PHOTO_ID_FIELD_TOO_LONG =
    "List exceeds 255 characters (device limit). Remove IDs or shorten the list.";
  var PHOTO_LABEL_FIELD_TOO_LONG =
    "Labels exceed 255 characters (device limit). Shorten or remove labels.";

  function postTextValueSet(url, value, useQueryFallback) {
    var body = new URLSearchParams();
    body.set("value", value == null ? "" : String(value));
    var encoded = body.toString();
    var fullUrl = url;
    if (useQueryFallback) {
      var candidate = url + "?" + encoded;
      if (candidate.length <= 120) fullUrl = candidate;
    }
    return fetch(fullUrl, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: encoded
    }).then(function (r) {
      if (!r.ok) console.error("POST " + fullUrl + " failed: " + r.status);
      return r;
    }).catch(function (err) {
      console.error("POST " + fullUrl + " error:", err);
      showBanner("Failed to save setting", "error");
    });
  }

  function delayMs(ms) {
    return new Promise(function (resolve) { setTimeout(resolve, ms); });
  }

  function saveConnectionValue(path, value, useQueryFallback) {
    return postTextValueSet(path + "/set", value, useQueryFallback).then(function (r) {
      if (!r || !r.ok) throw new Error("save_failed");
      return delayMs(1200);
    });
  }

  function connectionResponseValue(resp) {
    return (resp && (resp.value || resp.state)) || "";
  }

  function saveAndVerifyConnectionValue(path, value, useQueryFallback, isSaved) {
    return saveConnectionValue(path, value, useQueryFallback)
      .then(function () {
        return safeGet(path);
      })
      .then(function (resp) {
        var saved = connectionResponseValue(resp);
        if (isSaved && !isSaved(saved)) throw new Error("verify_failed");
        return saved;
      });
  }

  function saveAndVerifyConnection(url, key) {
    var normalizedUrl = normalizeImmichUrl(url);
    var apiKey = String(key || "").trim();
    if (!normalizedUrl || !apiKey) return Promise.reject(new Error("missing_connection"));
    return saveConnectionValue(endpoints.immich_url, normalizedUrl, true)
      .then(function () {
        return saveConnectionValue(endpoints.api_key, apiKey, false);
      })
      .then(function () {
        return Promise.all([
          safeGet(endpoints.immich_url),
          safeGet(endpoints.api_key)
        ]);
      })
      .then(function (res) {
        var savedUrl = normalizeImmichUrl(connectionResponseValue(res[0]));
        var savedKey = connectionResponseValue(res[1]);
        if (savedUrl !== normalizedUrl || !savedKey) throw new Error("verify_failed");
        S.immich_url = normalizedUrl;
        S.api_key = apiKey;
        return { url: normalizedUrl, key: apiKey };
      });
  }

  var PHOTO_SOURCE_APPLY_SETTING_KEYS = [
    "photo_source",
    "album_order",
    "album_ids",
    "person_ids",
    "tag_ids",
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

  function saveGenericSetting(key, value) {
    if (!key || !endpoints[key]) return Promise.resolve(null);
    var domain = settingEntityDomain(key);
    var savedValue = value;
    if (domain === "switch") savedValue = !!value;
    if (domain === "number") {
      var numberValue = Number(value);
      if (isFinite(numberValue)) savedValue = numberValue;
    }
    if (domain === "select" || domain === "text") savedValue = value == null ? "" : String(value);
    S[key] = savedValue;
    if (domain === "switch") {
      return post(endpoints[key] + (savedValue ? "/turn_on" : "/turn_off"));
    }
    if (domain === "select") {
      return post(endpoints[key] + "/set", { option: savedValue });
    }
    if (domain === "number") {
      return post(endpoints[key] + "/set", { value: savedValue });
    }
    if (domain === "text") {
      return postTextValueSet(endpoints[key] + "/set", savedValue);
    }
    return Promise.resolve(null);
  }

  function saveNtpServer(key, value) {
    var server = normalizeNtpServer(value);
    S[key] = server;
    return postTextValueSet(endpoints[key] + "/set", server);
  }

  function saveScheduleWakeTimeoutSetting(key, value) {
    return saveGenericSetting(key, normalizeScheduleWakeTimeout(value));
  }

  function saveScreenRotationSetting(key, value) {
    var rotation = String(value);
    if (screenRotationOptionsForUi().indexOf(rotation) === -1) return Promise.resolve(null);
    S.screen_rotation = rotation;
    S.portrait_pairing = !isPortraitScreenRotation(rotation);
    return Promise.all([
      saveGenericSetting("screen_rotation", rotation),
      saveGenericSetting("portrait_pairing", S.portrait_pairing)
    ]);
  }

  var SETTING_SAVE_ADAPTERS = {
    ntp_server_1: saveNtpServer,
    ntp_server_2: saveNtpServer,
    ntp_server_3: saveNtpServer,
    schedule_wake_timeout: saveScheduleWakeTimeoutSetting,
    screen_rotation: saveScreenRotationSetting
  };

  function saveSetting(key, value, options) {
    var opts = options || {};
    var adapter = SETTING_SAVE_ADAPTERS[key];
    var result = adapter ? adapter(key, value, opts) : saveGenericSetting(key, value);
    if (!opts.applyPhotoSource || !settingUsesPhotoSourceApply(key)) return result;
    return Promise.resolve(result).then(function (saved) {
      return post(endpoints.apply_photo_source + "/press").then(function () {
        return saved;
      });
    });
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

  __ESPFRAME_WEB_COMPAT_HELPERS__

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
    return fetch(url)
      .then(function (r) {
        if (!r.ok) return null;
        return r.json();
      })
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

  __ESPFRAME_WEB_BACKUP_IMPORT__

  // --- Init ---

  buildUI();
  initSSE();
})();
