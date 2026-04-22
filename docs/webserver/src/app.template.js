(function () {
  "use strict";

  var TIMEZONES = __ESPFRAME_TIMEZONES__;

  var S = {
    clock_options: ["24 Hour", "12 Hour"],
    tz_options: TIMEZONES,
    interval: "15 seconds",
    interval_options: [
      "10 seconds", "15 seconds", "20 seconds", "30 seconds", "45 seconds",
      "1 minute", "2 minutes", "3 minutes", "5 minutes", "10 minutes"
    ],
    conn_timeout: "2 minutes",
    conn_timeout_options: [
      "30 seconds", "45 seconds", "1 minute", "2 minutes", "3 minutes",
      "5 minutes", "10 minutes", "15 minutes", "20 minutes", "30 minutes"
    ],
    brightness: 100,
    backlight_on: true,
    show_clock: true,
    clock_format: "24 Hour",
    immich_url: "",
    api_key: "",
    timezone: "Europe/London (GMT+0)",
    firmware: "",
    installed_version: "",
    latest_version: "",
    update_available: false,
    beta_version: "",
    beta_available: false,
    auto_update: true,
    update_frequency: "Daily",
    update_freq_options: ["Hourly", "Daily", "Weekly", "Monthly"],
    schedule_enabled: false,
    schedule_on_hour: 6,
    schedule_off_hour: 23,
    schedule_wake_timeout: 60,
    brightness_current: 0,
    brightness_day: 100,
    brightness_night: 75,
    sunrise: "",
    sunset: "",
    photo_source: "All Photos",
    photo_source_options: ["All Photos", "Favorites", "Album", "Person", "Memories"],
    album_ids: "",
    album_labels: "",
    person_ids: "",
    person_labels: "",
    base_tone_enabled: false,
    base_tone: 0,
    warm_tones_enabled: false,
    warm_tone_intensity: 50,
    warm_tone_override: false,
    date_filter_enabled: false,
    date_filter_mode: "Fixed Range",
    date_filter_mode_options: ["Fixed Range", "Relative Range"],
    date_from: "",
    date_to: "",
    relative_amount: 1,
    relative_unit: "Years",
    relative_unit_options: ["Months", "Years"],
    portrait_pairing: true,
    photo_orientation: "Any",
    photo_orientation_options: ["Any", "Portrait Only", "Landscape Only"],
    display_mode: "Fill",
    display_mode_options: ["Fill", "Fit"],
    photo_metadata_enabled: true,
    photo_metadata_time_enabled: true,
    photo_metadata_location_enabled: true,
    screen_rotation: "0",
    screen_rotation_options: ["0", "180"],
  };

  var CSS = __ESPFRAME_CSS__;

  var style = document.createElement("style");
  style.textContent = CSS;
  document.head.appendChild(style);

  var fonts = document.createElement("link");
  fonts.rel = "stylesheet";
  fonts.href = "https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap";
  document.head.appendChild(fonts);

  var els = {};
  var app;

  function buildUI() {
    var root = document.createElement("div");
    root.id = "sp-app";

    var banner = document.createElement("div");
    banner.className = "banner";
    banner.style.display = "none";
    root.appendChild(banner);
    els.banner = banner;

    buildHeader(root);
    buildImmichPage(root);
    buildSettingsPage(root);
    buildLogsPage(root);

    var espApp = document.querySelector("esp-app");
    if (espApp) {
      espApp.parentNode.insertBefore(root, espApp);
    } else {
      document.body.insertBefore(root, document.body.firstChild);
    }
    els.root = root;
    switchTab("immich");
  }

  function buildHeader(parent) {
    var header = document.createElement("div");
    header.className = "sp-header";

    var brand = document.createElement("div");
    brand.className = "sp-brand";
    brand.textContent = "EspFrame";
    header.appendChild(brand);

    var nav = document.createElement("nav");
    nav.className = "sp-nav";

    var tabs = [
      { id: "immich", label: "Immich" },
      { id: "settings", label: "Device" },
      { id: "logs", label: "Logs" }
    ];

    tabs.forEach(function (t) {
      var tab = document.createElement("div");
      tab.className = "sp-tab";
      tab.textContent = t.label;
      tab.addEventListener("click", function () { switchTab(t.id); });
      nav.appendChild(tab);
      els["tab_" + t.id] = tab;
    });

    header.appendChild(nav);
    parent.appendChild(header);
  }

  var immichApp;

  function buildImmichPage(parent) {
    var page = document.createElement("div");
    page.id = "sp-immich";
    page.className = "sp-page";

    var wrap = document.createElement("div");
    wrap.className = "sp-settings-wrap";
    page.appendChild(wrap);

    parent.appendChild(page);
    els.immichPage = page;
    immichApp = wrap;
  }

  function buildSettingsPage(parent) {
    var page = document.createElement("div");
    page.id = "sp-settings";
    page.className = "sp-page";

    var wrap = document.createElement("div");
    wrap.className = "sp-settings-wrap";
    page.appendChild(wrap);

    parent.appendChild(page);
    els.settingsPage = page;
    app = wrap;
  }

  function buildLogsPage(parent) {
    var page = document.createElement("div");
    page.id = "sp-logs";
    page.className = "sp-page";

    var toolbar = document.createElement("div");
    toolbar.className = "sp-log-toolbar";
    var clearBtn = document.createElement("button");
    clearBtn.className = "sp-log-clear";
    clearBtn.textContent = "Clear";
    clearBtn.addEventListener("click", function () { els.logOutput.innerHTML = ""; });
    toolbar.appendChild(clearBtn);
    page.appendChild(toolbar);

    var output = document.createElement("div");
    output.className = "sp-log-output";
    page.appendChild(output);
    els.logOutput = output;

    parent.appendChild(page);
    els.logsPage = page;
  }

  function switchTab(tab) {
    ["immich", "settings", "logs"].forEach(function (t) {
      els["tab_" + t].className = "sp-tab" + (tab === t ? " active" : "");
    });
    els.immichPage.className = "sp-page" + (tab === "immich" ? " active" : "");
    els.settingsPage.className = "sp-page" + (tab === "settings" ? " active" : "");
    els.logsPage.className = "sp-page" + (tab === "logs" ? " active" : "");
  }

  function eid(domain, name) {
    return "/" + domain + "/" + encodeURIComponent(name);
  }

  var endpoints = {
    immich_url: eid("text", "Connection: Server URL"),
    api_key: eid("text", "Connection: API Key"),
    clock_format: eid("select", "Clock: Format"),
    timezone: eid("select", "Clock: Timezone"),
    interval: eid("select", "Photos: Slideshow Interval"),
    conn_timeout: eid("select", "Screen: Connection Timeout"),
    backlight: eid("light", "Screen: Backlight"),
    show_clock: eid("switch", "Clock: Show"),
    firmware: eid("text_sensor", "Firmware: Version"),
    update: eid("update", "Firmware: Update"),
    update_beta: eid("update", "Firmware: Update Beta"),
    auto_update: eid("switch", "Firmware: Auto Update"),
    update_frequency: eid("select", "Firmware: Update Frequency"),
    schedule_enabled: eid("switch", "Screen: Schedule Enabled"),
    schedule_on_hour: eid("number", "Screen: Schedule On Hour"),
    schedule_off_hour: eid("number", "Screen: Schedule Off Hour"),
    schedule_wake_timeout: eid("number", "Screen: Schedule Wake Timeout"),
    brightness_day: eid("number", "Screen: Daytime Brightness"),
    brightness_night: eid("number", "Screen: Nighttime Brightness"),
    sunrise: eid("text_sensor", "Screen: Sunrise"),
    sunset: eid("text_sensor", "Screen: Sunset"),
    photo_source: eid("select", "Photos: Source"),
    album_ids: eid("text", "Photos: Album IDs"),
    album_labels: eid("text", "Photos: Album Labels"),
    person_ids: eid("text", "Photos: Person IDs"),
    person_labels: eid("text", "Photos: Person Labels"),
    date_filter_enabled: eid("switch", "Photos: Date Filter"),
    date_filter_mode: eid("select", "Photos: Date Filter Mode"),
    date_from: eid("text", "Photos: Date From"),
    date_to: eid("text", "Photos: Date To"),
    relative_amount: eid("number", "Photos: Relative Amount"),
    relative_unit: eid("select", "Photos: Relative Unit"),
    base_tone_enabled: eid("switch", "Screen: Tone Adjustment"),
    base_tone: eid("number", "Screen: Display Tone"),
    warm_tones_enabled: eid("switch", "Screen: Night Tone Adjustment"),
    warm_tone_intensity: eid("number", "Screen: Warm Tone Intensity"),
    warm_tone_override: eid("switch", "Screen: Warm Tone Override"),
    portrait_pairing: eid("switch", "Photos: Portrait Pairing"),
    photo_orientation: eid("select", "Photos: Orientation"),
    display_mode: eid("select", "Photos: Display Mode"),
    photo_metadata_enabled: eid("switch", "Device: Show Metadata"),
    photo_metadata_time_enabled: eid("switch", "Device: Metadata Time"),
    photo_metadata_location_enabled: eid("switch", "Device: Metadata Location"),
    screen_rotation: eid("select", "Screen: Rotation"),
  };

  function post(url, params) {
    var fullUrl = params ? url + "?" + new URLSearchParams(params).toString() : url;
    return fetch(fullUrl, { method: "POST" }).then(function (r) {
      if (!r.ok) console.error("POST " + fullUrl + " failed: " + r.status);
      return r;
    }).catch(function (err) {
      console.error("POST " + fullUrl + " error:", err);
      showBanner("Failed to save setting", "error");
    });
  }

  function postScheduleWakeTimeout(value) {
    var seconds = normalizeScheduleWakeTimeout(value);
    post(endpoints.schedule_wake_timeout + "/set", { value: seconds });
  }

  // Matches the ESPHome template text max_length for album/person ID and label lists.
  var MAX_PHOTO_ID_FIELD_LENGTH = 255;
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

  function stripUrlTrailingSlashes(value) {
    var url = String(value == null ? "" : value);
    while (url.length > 0 && url.charAt(url.length - 1) === "/" && !/^[a-z][a-z0-9+.-]*:\/\/$/i.test(url)) {
      url = url.slice(0, -1);
    }
    return url;
  }

  function extractUrlAuthority(value) {
    var url = String(value || "");
    if (url.indexOf("//") === 0) url = url.slice(2);
    return url.split(/[/?#]/)[0] || "";
  }

  function extractUrlHost(value) {
    var authority = extractUrlAuthority(value);
    var at = authority.lastIndexOf("@");
    if (at >= 0) authority = authority.slice(at + 1);
    if (!authority) return "";
    if (authority.charAt(0) === "[") {
      var close = authority.indexOf("]");
      return (close >= 0 ? authority.slice(0, close + 1) : authority).toLowerCase();
    }
    return authority.split(":")[0].toLowerCase();
  }

  function extractUrlPort(value) {
    var authority = extractUrlAuthority(value);
    var at = authority.lastIndexOf("@");
    if (at >= 0) authority = authority.slice(at + 1);
    if (!authority) return "";
    if (authority.charAt(0) === "[") {
      var close = authority.indexOf("]");
      if (close >= 0 && authority.charAt(close + 1) === ":") return authority.slice(close + 2).match(/^\d*/)[0];
      return "";
    }
    var colon = authority.indexOf(":");
    return colon >= 0 ? authority.slice(colon + 1).match(/^\d*/)[0] : "";
  }

  function urlHasExplicitPort(value) {
    return extractUrlPort(value) !== "";
  }

  function isLocalImmichHost(host) {
    if (!host) return false;
    if (host === "localhost" || host.charAt(0) === "[") return true;
    if (/^\d{1,3}(\.\d{1,3}){3}$/.test(host)) return true;
    return host.slice(-6) === ".local" || host.slice(-4) === ".lan";
  }

  function normalizeImmichUrl(value) {
    var url = stripUrlTrailingSlashes(String(value == null ? "" : value).trim());
    if (!url) return "";
    if (url.indexOf("//") === 0) {
      url = "https:" + url;
    } else if (!/^[a-z][a-z0-9+.-]*:\/\//i.test(url)) {
      var host = extractUrlHost(url);
      var port = extractUrlPort(url);
      var useHttp = isLocalImmichHost(host) || urlHasExplicitPort(url);
      if (port === "443") useHttp = false;
      url = (useHttp ? "http://" : "https://") + url;
    }
    return stripUrlTrailingSlashes(url.replace(/^([a-z][a-z0-9+.-]*):\/\//i, function (_, scheme) {
      return scheme.toLowerCase() + "://";
    }));
  }

  function photoIdFieldTooLong(s) {
    return String(s != null ? s : "").trim().length > MAX_PHOTO_ID_FIELD_LENGTH;
  }

  function photoLabelFieldTooLong(s) {
    return String(s != null ? s : "").trim().length > MAX_PHOTO_ID_FIELD_LENGTH;
  }

  var UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
  function isValidUuidList(str) {
    var s = str.trim();
    if (!s) return true;
    return s.split(",").every(function (id) { return UUID_RE.test(id.trim()); });
  }

  function splitPhotoIdList(str) {
    var parts = String(str || "").split(",").map(function (id) {
      return id.trim();
    }).filter(Boolean);
    return parts.length ? parts : [""];
  }

  function parsePhotoLabelList(str) {
    var raw = String(str || "").trim();
    if (!raw) return [];
    try {
      var parsed = JSON.parse(raw);
      if (Array.isArray(parsed)) return parsed.map(function (label) { return String(label || ""); });
    } catch (_) {}
    return raw.split(",").map(function (label) { return label.trim(); });
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

  var evtSource = null;
  var rendered = false;
  var renderTimer = null;
  var renderAttemptInFlight = false;
  var initialSettingsRefreshStarted = false;
  var logListenerAttached = false;

  var ANSI_LEVEL = {
    "1;31": "sp-log-error",
    "0;31": "sp-log-error",
    "0;33": "sp-log-warn",
    "0;32": "sp-log-info",
    "0;35": "sp-log-config",
    "0;36": "sp-log-debug",
    "0;37": "sp-log-verbose"
  };
  var ANSI_RE = /\033\[[\d;]*m/g;

  function appendLog(msg, lvl) {
    if (!els.logOutput) return;
    var line = document.createElement("div");
    line.className = "sp-log-line";

    var ansiClass = "";
    var m = msg.match(/\033\[([\d;]+)m/);
    if (m) ansiClass = ANSI_LEVEL[m[1]] || "";

    if (ansiClass) {
      line.classList.add(ansiClass);
    } else if (lvl === 1) line.classList.add("sp-log-error");
    else if (lvl === 2) line.classList.add("sp-log-warn");
    else if (lvl === 3) line.classList.add("sp-log-info");
    else if (lvl === 4) line.classList.add("sp-log-config");
    else if (lvl === 5) line.classList.add("sp-log-debug");
    else if (lvl >= 6) line.classList.add("sp-log-verbose");

    line.textContent = msg.replace(ANSI_RE, "");

    var atBottom = els.logOutput.scrollHeight - els.logOutput.scrollTop - els.logOutput.clientHeight < 40;
    els.logOutput.appendChild(line);
    var overflow = els.logOutput.childNodes.length - 1000;
    if (overflow > 0) {
      for (var i = 0; i < overflow; i++)
        els.logOutput.removeChild(els.logOutput.firstChild);
    }
    if (atBottom) els.logOutput.scrollTop = els.logOutput.scrollHeight;
  }

  // Entity id -> state key mapping; optional optionsKey and default.
  var ENTITY_STATE_MAP = {
    "text/Connection: Server URL": { key: "immich_url" },
    "text/Connection: API Key": { key: "api_key" },
    "select/Clock: Format": { key: "clock_format", optionsKey: "clock_options", default: "24 Hour" },
    "select/Clock: Timezone": { key: "timezone", optionsKey: "tz_options", default: "" },
    "select/Photos: Slideshow Interval": { key: "interval", optionsKey: "interval_options", default: "2 minutes" },
    "select/Screen: Connection Timeout": { key: "conn_timeout", optionsKey: "conn_timeout_options", default: "2 minutes" },
    "switch/Clock: Show": { key: "show_clock", boolFromState: true },
    "text_sensor/Firmware: Version": { key: "firmware" },
    "switch/Firmware: Auto Update": { key: "auto_update", boolFromState: true },
    "select/Firmware: Update Frequency": { key: "update_frequency", optionsKey: "update_freq_options", default: "Daily" },
    "switch/Screen: Schedule Enabled": { key: "schedule_enabled", boolFromState: true },
    "switch/Screen: Schedule": { key: "schedule_enabled", boolFromState: true },
    "number/Screen: Schedule On Hour": { key: "schedule_on_hour", default: 6, number: true },
    "number/Screen: Schedule On": { key: "schedule_on_hour", default: 6, number: true },
    "number/Screen: Schedule Off Hour": { key: "schedule_off_hour", default: 23, number: true },
    "number/Screen: Schedule Off": { key: "schedule_off_hour", default: 23, number: true },
    "number/Screen: Schedule Wake Timeout": { key: "schedule_wake_timeout", default: 60, number: true },
    "number/Screen: Daytime Brightness": { key: "brightness_day", default: 100, number: true },
    "number/Screen: Nighttime Brightness": { key: "brightness_night", default: 75, number: true },
    "text_sensor/Screen: Sunrise": { key: "sunrise" },
    "text_sensor/Screen: Sunset": { key: "sunset" },
    "select/Photos: Source": { key: "photo_source", optionsKey: "photo_source_options", default: "All Photos" },
    "text/Photos: Album IDs": { key: "album_ids" },
    "text/Photos: Album Labels": { key: "album_labels" },
    "text/Photos: Person IDs": { key: "person_ids" },
    "text/Photos: Person Labels": { key: "person_labels" },
    "switch/Photos: Date Filter": { key: "date_filter_enabled", boolFromState: true },
    "select/Photos: Date Filter Mode": { key: "date_filter_mode", optionsKey: "date_filter_mode_options", default: "Fixed Range" },
    "text/Photos: Date From": { key: "date_from" },
    "text/Photos: Date To": { key: "date_to" },
    "number/Photos: Relative Amount": { key: "relative_amount", default: 1, number: true },
    "select/Photos: Relative Unit": { key: "relative_unit", optionsKey: "relative_unit_options", default: "Years" },
    "select/Photos: Orientation": { key: "photo_orientation", optionsKey: "photo_orientation_options", default: "Any" },
    "switch/Screen: Tone Adjustment": { key: "base_tone_enabled", boolFromState: true },
    "number/Screen: Display Tone": { key: "base_tone", default: 0, number: true },
    "switch/Screen: Night Tone Adjustment": { key: "warm_tones_enabled", boolFromState: true },
    "number/Screen: Warm Tone Intensity": { key: "warm_tone_intensity", default: 50, number: true },
    "switch/Screen: Warm Tone Override": { key: "warm_tone_override", boolFromState: true },
    "select/Screen: Rotation": { key: "screen_rotation", optionsKey: "screen_rotation_options", default: "0" },
    "switch/Photos: Portrait Pairing": { key: "portrait_pairing", boolFromState: true },
    "select/Photos: Display Mode": { key: "display_mode", optionsKey: "display_mode_options", default: "Fill" },
    "switch/Device: Show Metadata": { key: "photo_metadata_enabled", boolFromState: true },
    "switch/Device: Metadata Time": { key: "photo_metadata_time_enabled", boolFromState: true },
    "switch/Device: Metadata Location": { key: "photo_metadata_location_enabled", boolFromState: true }
  };

  function applyEntityToState(d) {
    if (!d || !d.id) return;
    var id = d.id;
    if (id === "light/Screen: Backlight") {
      S.backlight_on = d.state === "ON";
      if (d.brightness != null) {
        S.brightness = Math.round((d.brightness / 255) * 100);
        S.brightness_current = S.brightness;
      }
      return;
    }
    if (id === "update/Firmware: Update") {
      S.installed_version = d.current_version || "";
      S.latest_version = d.latest_version || "";
      S.update_available =
        S.installed_version &&
        S.latest_version &&
        S.installed_version !== S.latest_version;
      return;
    }
    if (id === "update/Firmware: Update Beta") {
      S.beta_version = d.latest_version || "";
      S.beta_available =
        S.beta_version &&
        d.current_version &&
        S.beta_version !== d.current_version;
      return;
    }
    var spec = ENTITY_STATE_MAP[id];
    if (!spec) return;
    var v = d.value != null ? d.value : d.state;
    if (spec.boolFromState) {
      S[spec.key] = v === true || v === "ON";
    } else if (spec.number) {
      S[spec.key] = v != null ? Math.round(Number(v)) : (spec.default !== undefined ? spec.default : 0);
    } else {
      S[spec.key] = v !== undefined && v !== null ? String(v) : (spec.default !== undefined ? spec.default : "");
    }
    if (spec.optionsKey && d.option && d.option.length) S[spec.optionsKey] = d.option;
  }

  function collectState(d) {
    applyEntityToState(d);
  }

  // Single source for settings fetched on load; KEY_TO_ENTITY_ID derived from ENTITY_STATE_MAP.
  var INITIAL_FETCH_KEYS = [
    "firmware", "auto_update", "update_frequency",
    "clock_format", "timezone",
    "photo_source", "album_ids", "album_labels", "person_ids", "person_labels",
    "date_filter_enabled", "date_filter_mode", "date_from", "date_to", "relative_amount", "relative_unit",
    "photo_orientation",
    "interval", "conn_timeout",
    "schedule_enabled", "schedule_on_hour", "schedule_off_hour", "schedule_wake_timeout",
    "sunrise", "sunset",
    "base_tone_enabled", "base_tone", "warm_tones_enabled", "warm_tone_intensity", "warm_tone_override",
    "screen_rotation",
    "portrait_pairing",
    "display_mode",
    "photo_metadata_enabled", "photo_metadata_time_enabled", "photo_metadata_location_enabled"
  ];
  function getEntityIdForStateKey(key) {
    for (var id in ENTITY_STATE_MAP) {
      if (ENTITY_STATE_MAP[id].key === key) return id;
    }
    return null;
  }
  var KEY_TO_ENTITY_ID = {};
  INITIAL_FETCH_KEYS.forEach(function (k) {
    var id = getEntityIdForStateKey(k);
    if (id) KEY_TO_ENTITY_ID[k] = id;
  });

  function fetchDeviceSettingsState() {
    var urls = INITIAL_FETCH_KEYS.map(function (k) { return safeGet(endpoints[k]); });
    return Promise.all(urls).then(function (res) {
      for (var i = 0; i < res.length; i++) {
        var data = res[i];
        if (!data) continue;
        applyEntityToState({
          id: KEY_TO_ENTITY_ID[INITIAL_FETCH_KEYS[i]],
          value: data.value,
          state: data.state,
          option: data.option
        });
      }
    });
  }

  function isEditingSetting() {
    var active = document.activeElement;
    if (!active || !els.root || !els.root.contains(active)) return false;
    return /^(INPUT|SELECT|TEXTAREA|BUTTON)$/.test(active.tagName);
  }

  function renderConfiguredSettingsPage() {
    renderSettings();

    if (initialSettingsRefreshStarted) return;
    initialSettingsRefreshStarted = true;

    // Draw the cards first. The ESP webserver can take a while to answer every
    // per-entity request, so hydrate the values in the background.
    fetchDeviceSettingsState().then(function () {
      if (rendered && !isEditingSetting()) renderSettings();
    });
  }

  function scheduleTryRender(delayMs) {
    if (rendered || renderAttemptInFlight || renderTimer) return;
    renderTimer = setTimeout(function () {
      renderTimer = null;
      tryRender();
    }, delayMs);
  }

  function showConfiguredSettings() {
    rendered = true;
    renderAttemptInFlight = false;
    renderConfiguredSettingsPage();
  }

  function tryRender() {
    if (rendered || renderAttemptInFlight) return;
    if (renderTimer) {
      clearTimeout(renderTimer);
      renderTimer = null;
    }
    if (S.immich_url) {
      showConfiguredSettings();
      return;
    }
    renderAttemptInFlight = true;
    Promise.all([
      safeGet(endpoints.immich_url),
      safeGet(endpoints.api_key)
    ]).then(function (res) {
      renderAttemptInFlight = false;
      if (rendered) return;
      if (res[0]) S.immich_url = normalizeImmichUrl(res[0].value || res[0].state || "");
      if (res[1]) S.api_key = res[1].value || res[1].state || "";
      if (S.immich_url) {
        showConfiguredSettings();
      } else {
        rendered = true;
        renderWizard();
      }
    });
  }

  function initSSE() {
    try {
      evtSource = new EventSource("/events");

      evtSource.addEventListener("state", function (e) {
        try {
          var d = JSON.parse(e.data);
          collectState(d);
          if (rendered) handleLiveEvent(d);
        } catch (_) {}

        if (!rendered) {
          if (S.immich_url) showConfiguredSettings();
          else scheduleTryRender(250);
        }
      });

      if (!logListenerAttached) {
        logListenerAttached = true;
        evtSource.addEventListener("log", function (e) {
          var d;
          try { d = JSON.parse(e.data); } catch (_) { d = { msg: e.data }; }
          appendLog(d.msg || e.data, d.lvl);
        });
      }

      evtSource.onerror = function () {
        if (!rendered) {
          scheduleTryRender(1000);
        }
      };

      evtSource.onopen = function () {};
    } catch (_) {
      tryRender();
    }

    scheduleTryRender(250);
    setTimeout(function () {
      if (!rendered) tryRender();
    }, 5000);
  }

  // --- Wizard ---

  function renderWizard() {
    var step = 1;
    immichApp.innerHTML = "";
    app.innerHTML = "";
    renderStartupDevicePage();
    var wrap = el("div", "fade-in");
    wrap.innerHTML =
      '<p class="subtitle">Let\'s connect your photo frame</p>';
    var steps = el("div", "wizard-steps");
    var s1 = el("div", "step active");
    var s2 = el("div", "step");
    steps.appendChild(s1);
    steps.appendChild(s2);
    wrap.appendChild(steps);

    var body = el("div");
    wrap.appendChild(body);
    immichApp.appendChild(wrap);

    function showStep() {
      body.innerHTML = "";
      if (step === 1) {
        s1.className = "step active";
        s2.className = "step";
        body.appendChild(renderStep1());
      } else {
        s1.className = "step done";
        s2.className = "step active";
        body.appendChild(renderStep2());
      }
    }

    function renderStep1() {
      var card = el("div", "card fade-in");
      card.innerHTML = "<h3>Connection</h3>";

      var f1 = field("Immich Server URL");
      var urlInput = input("url", S.immich_url, "https://photos.example.com");
      f1.appendChild(urlInput);
      card.appendChild(f1);

      var f2 = field("API Key");
      var grp = el("div", "input-group");
      var keyInput = input("password", S.api_key, "Your Immich API key");
      var showBtn = el("button", "btn btn-secondary");
      showBtn.textContent = "Show";
      showBtn.type = "button";
      showBtn.onclick = function () {
        var isPass = keyInput.type === "password";
        keyInput.type = isPass ? "text" : "password";
        showBtn.textContent = isPass ? "Hide" : "Show";
      };
      grp.appendChild(keyInput);
      grp.appendChild(showBtn);
      f2.appendChild(grp);
      card.appendChild(f2);

      var nav = el("div", "wizard-nav");
      var nextBtn = el("button", "btn btn-primary");
      nextBtn.textContent = "Connect";
      nextBtn.onclick = function () {
        var u = normalizeImmichUrl(urlInput.value);
        var k = keyInput.value.trim();
        if (!u || !k) return;
        nextBtn.disabled = true;
        nextBtn.textContent = "Saving\u2026";
        postTextValueSet(endpoints.immich_url + "/set", u, true)
          .then(function () {
            return new Promise(function (r) { setTimeout(r, 500); });
          })
          .then(function () {
            return postTextValueSet(endpoints.api_key + "/set", k, true);
          })
          .then(function () {
            S.immich_url = u;
            S.api_key = k;
            step = 2;
            showStep();
          });
      };
      nav.appendChild(nextBtn);
      card.appendChild(nav);
      return card;
    }

    function renderStep2() {
      var card = el("div", "card fade-in");
      card.innerHTML = "<h3>Clock & timezone</h3>";

      var f1 = field("Clock Format");
      f1.appendChild(
        selectFromOptions(S.clock_options, S.clock_format, function (v) {
          S.clock_format = v;
          post(endpoints.clock_format + "/set", { option: v });
        })
      );
      card.appendChild(f1);

      var f2 = field("Timezone");
      f2.appendChild(
        timezoneSelect(S.tz_options, S.timezone, function (v) {
          post(endpoints.timezone + "/set", { option: v });
          S.timezone = v;
        })
      );
      card.appendChild(f2);

      var nav = el("div", "wizard-nav");
      var backBtn = el("button", "btn btn-secondary");
      backBtn.textContent = "Back";
      backBtn.onclick = function () {
        step = 1;
        showStep();
      };
      var doneBtn = el("button", "btn btn-primary");
      doneBtn.textContent = "Done";
      doneBtn.onclick = function () {
        renderSettings();
      };
      nav.appendChild(backBtn);
      nav.appendChild(doneBtn);
      card.appendChild(nav);
      return card;
    }

    showStep();
  }

  function renderStartupDevicePage() {
    var wrap = el("div", "fade-in");
    wrap.appendChild(makeImportSettingsCard());
    app.appendChild(wrap);
  }

  // --- Settings ---

  function renderSettings() {
    app.innerHTML = "";
    immichApp.innerHTML = "";
    var immichWrap = el("div", "fade-in");
    var wrap = el("div", "fade-in");

    // Connection
    var connBody = el("div");
    var connStatus = el("div", "status mb-12");
    connStatus.id = "conn-status";

    function showSaved(msg) {
      connStatus.innerHTML = '<span class="dot green"></span> ' + (msg || "Saved");
      clearTimeout(connStatus._t);
      connStatus._t = setTimeout(function () {
        connStatus.textContent = "";
      }, 3000);
    }

    var f1 = field("Immich Server URL");
    var urlInput = input("url", S.immich_url, "https://photos.example.com");
    urlInput.onchange = function () {
      var u = normalizeImmichUrl(urlInput.value);
      urlInput.value = u;
      S.immich_url = u;
      postTextValueSet(endpoints.immich_url + "/set", u, true).then(function () {
        showSaved("URL saved");
      });
    };
    f1.appendChild(urlInput);
    connBody.appendChild(f1);

    var f2 = field("API Key");
    var keyConfigured = S.api_key && S.api_key.length > 0;
    var keyWrap = el("div");

    function showKeyMasked() {
      keyWrap.innerHTML = "";
      var row = el("div", "input-group");
      var mask = el("div");
      mask.className = "key-mask";
      mask.textContent = "\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022";
      var cb = el("button", "btn btn-secondary");
      cb.textContent = "Change";
      cb.type = "button";
      cb.onclick = function () {
        keyWrap.innerHTML = "";
        keyWrap.appendChild(makeKeyInput());
      };
      row.appendChild(mask);
      row.appendChild(cb);
      keyWrap.appendChild(row);
    }

    function makeKeyInput() {
      var grp = el("div", "input-group");
      var keyInput = input("text", "", "Paste your Immich API key");
      var saveBtn = el("button", "btn btn-primary");
      saveBtn.textContent = "Save";
      saveBtn.type = "button";
      saveBtn.onclick = function () {
        var v = keyInput.value.trim();
        if (!v) return;
        saveBtn.disabled = true;
        saveBtn.textContent = "Saving\u2026";
        postTextValueSet(endpoints.api_key + "/set", v, true).then(function () {
          showSaved("API key saved");
          showKeyMasked();
        });
      };
      grp.appendChild(keyInput);
      grp.appendChild(saveBtn);
      return grp;
    }

    if (keyConfigured) {
      showKeyMasked();
    } else {
      keyWrap.appendChild(makeKeyInput());
    }

    f2.appendChild(keyWrap);
    connBody.appendChild(f2);

    var fConnTimeout = field("Connection Timeout");
    fConnTimeout.appendChild(
      selectFromOptions(S.conn_timeout_options, S.conn_timeout, function (v) {
        S.conn_timeout = v;
        post(endpoints.conn_timeout + "/set", { option: v });
      })
    );
    connBody.appendChild(fConnTimeout);

    connBody.appendChild(connStatus);
    immichWrap.appendChild(makeCollapsibleCard("Connection", connBody, true));

    // Frequency
    var dispBody = el("div");
    var f3 = field("Slideshow Interval");
    f3.appendChild(
      selectFromOptions(S.interval_options, S.interval, function (v) {
        S.interval = v;
        post(endpoints.interval + "/set", { option: v });
      })
    );
    dispBody.appendChild(f3);
    immichWrap.appendChild(makeCollapsibleCard("Frequency", dispBody, true));

    // Photo Source
    var srcBody = el("div");
    var photoSourceApplyTimer = null;
    var pendingPhotoSourceSave = { source: false, album: false, albumLabel: false, person: false, personLabel: false };
    var fSrc = field("Source");
    var srcSel = selectFromOptions(S.photo_source_options, S.photo_source, function (v) {
      S.photo_source = v;
      albumField.style.display = v === "Album" ? "" : "none";
      personField.style.display = v === "Person" ? "" : "none";
      schedulePhotoSourceApply(0, { source: true });
    });

    var removeIdIcon = "<svg viewBox=\"0 0 24 24\" width=\"18\" height=\"18\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M3 6h18\"/><path d=\"M8 6V4h8v2\"/><path d=\"M19 6l-1 14H6L5 6\"/><path d=\"M10 11v5\"/><path d=\"M14 11v5\"/></svg>";

    var albumField = field("Albums");
    var albumIdList = el("div", "photo-id-list");
    var albumInputs = [];
    var albumLabelInputs = [];
    var albumError = el("div", "field-error");
    function getAlbumIdsValue() {
      return albumInputs.map(function (inputEl) {
        return inputEl.value.trim();
      }).filter(Boolean).join(",");
    }
    function getAlbumLabelsValue() {
      return buildPhotoLabelList(albumInputs, albumLabelInputs);
    }
    function refreshAlbumRemoveButtons() {
      Array.prototype.forEach.call(albumIdList.querySelectorAll(".album-id-remove"), function (btn) {
        btn.disabled = albumInputs.length <= 1;
      });
    }
    function addAlbumIdRow(value, labelValue) {
      var row = el("div", "photo-id-row");
      var fields = el("div", "photo-id-fields");
      var albumInput = input("text", value || "", "Paste album ID from Immich URL", MAX_PHOTO_ID_FIELD_LENGTH);
      var albumLabelInput = input("text", labelValue || "", "What is it?", MAX_PHOTO_ID_FIELD_LENGTH);
      var removeBtn = el("button", "btn btn-secondary btn-icon album-id-remove");
      removeBtn.type = "button";
      removeBtn.innerHTML = removeIdIcon;
      removeBtn.title = "Remove album ID";
      removeBtn.setAttribute("aria-label", "Remove album ID");
      removeBtn.onclick = function () {
        if (albumInputs.length <= 1) {
          albumInput.value = "";
          albumLabelInput.value = "";
          schedulePhotoSourceApply(0, { album: true, albumLabel: true });
          return;
        }
        var removeIndex = albumInputs.indexOf(albumInput);
        albumInputs.splice(removeIndex, 1);
        albumLabelInputs.splice(removeIndex, 1);
        row.parentNode.removeChild(row);
        refreshAlbumRemoveButtons();
        schedulePhotoSourceApply(0, { album: true, albumLabel: true });
      };
      albumInput.oninput = function () {
        schedulePhotoSourceApply(null, { album: true, albumLabel: true });
      };
      albumLabelInput.oninput = function () {
        schedulePhotoSourceApply(null, { albumLabel: true });
      };
      fields.appendChild(albumInput);
      fields.appendChild(albumLabelInput);
      row.appendChild(fields);
      row.appendChild(removeBtn);
      albumIdList.appendChild(row);
      albumInputs.push(albumInput);
      albumLabelInputs.push(albumLabelInput);
      refreshAlbumRemoveButtons();
    }
    var albumIds = splitPhotoIdList(S.album_ids);
    var albumLabels = parsePhotoLabelList(S.album_labels);
    for (var albumIndex = 0; albumIndex < Math.max(albumIds.length, albumLabels.length, 1); albumIndex++) {
      addAlbumIdRow(albumIds[albumIndex] || "", albumLabels[albumIndex] || "");
    }
    var addAlbumRow = el("div", "photo-id-actions");
    var addAlbumBtn = el("button", "btn btn-secondary");
    addAlbumBtn.type = "button";
    addAlbumBtn.textContent = "Add an album";
    addAlbumBtn.title = "Add an album";
    addAlbumBtn.setAttribute("aria-label", "Add an album");
    addAlbumBtn.onclick = function () {
      addAlbumIdRow("", "");
      albumInputs[albumInputs.length - 1].focus();
    };
    addAlbumRow.appendChild(addAlbumBtn);
    albumField.appendChild(albumIdList);
    albumField.appendChild(addAlbumRow);
    albumField.appendChild(albumError);
    albumField.style.display = S.photo_source === "Album" ? "" : "none";

    var personField = field("People");
    var personIdList = el("div", "photo-id-list");
    var personInputs = [];
    var personLabelInputs = [];
    var personError = el("div", "field-error");
    function getPersonIdsValue() {
      return personInputs.map(function (inputEl) {
        return inputEl.value.trim();
      }).filter(Boolean).join(",");
    }
    function getPersonLabelsValue() {
      return buildPhotoLabelList(personInputs, personLabelInputs);
    }
    function validatePhotoSourceInputs(changes) {
      albumError.textContent = "";
      personError.textContent = "";
      var srcVal = srcSel.value;
      var albumTrim = getAlbumIdsValue();
      var albumLabels = getAlbumLabelsValue();
      var personTrim = getPersonIdsValue();
      var personLabels = getPersonLabelsValue();
      var shouldValidateAlbum = changes.album || srcVal === "Album";
      var shouldValidatePerson = changes.person || srcVal === "Person";
      if (shouldValidateAlbum && photoIdFieldTooLong(albumTrim)) {
        albumError.textContent = PHOTO_ID_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidatePerson && photoIdFieldTooLong(personTrim)) {
        personError.textContent = PHOTO_ID_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidateAlbum && !isValidUuidList(albumTrim)) {
        albumError.textContent = "Invalid UUID format";
        return null;
      }
      if (changes.albumLabel && photoLabelFieldTooLong(albumLabels)) {
        albumError.textContent = PHOTO_LABEL_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidatePerson && !isValidUuidList(personTrim)) {
        personError.textContent = "Invalid UUID format";
        return null;
      }
      if (changes.personLabel && photoLabelFieldTooLong(personLabels)) {
        personError.textContent = PHOTO_LABEL_FIELD_TOO_LONG;
        return null;
      }
      return { source: srcVal, albumIds: albumTrim, albumLabels: albumLabels, personIds: personTrim, personLabels: personLabels };
    }
    function applyPhotoSourceInputs() {
      var changes = {
        source: pendingPhotoSourceSave.source,
        album: pendingPhotoSourceSave.album,
        albumLabel: pendingPhotoSourceSave.albumLabel,
        person: pendingPhotoSourceSave.person,
        personLabel: pendingPhotoSourceSave.personLabel
      };
      pendingPhotoSourceSave = { source: false, album: false, albumLabel: false, person: false, personLabel: false };
      var vals = validatePhotoSourceInputs(changes);
      if (!vals) return;
      var requests = [];
      if (changes.source) {
        S.photo_source = vals.source;
        requests.push(post(endpoints.photo_source + "/set", { option: vals.source }));
      }
      if (changes.album) {
        S.album_ids = vals.albumIds;
        requests.push(postTextValueSet(endpoints.album_ids + "/set", vals.albumIds));
      }
      if (changes.albumLabel) {
        S.album_labels = vals.albumLabels;
        requests.push(postTextValueSet(endpoints.album_labels + "/set", vals.albumLabels));
      }
      if (changes.person) {
        S.person_ids = vals.personIds;
        requests.push(postTextValueSet(endpoints.person_ids + "/set", vals.personIds));
      }
      if (changes.personLabel) {
        S.person_labels = vals.personLabels;
        requests.push(postTextValueSet(endpoints.person_labels + "/set", vals.personLabels));
      }
      if (!requests.length) return;
      Promise.all(requests).then(function () {
        if (changes.source || changes.album || changes.person)
          post(eid("button", "Apply Photo Source") + "/press");
      });
    }
    function schedulePhotoSourceApply(delayMs, changes) {
      if (changes) {
        pendingPhotoSourceSave.source = pendingPhotoSourceSave.source || !!changes.source;
        pendingPhotoSourceSave.album = pendingPhotoSourceSave.album || !!changes.album;
        pendingPhotoSourceSave.albumLabel = pendingPhotoSourceSave.albumLabel || !!changes.albumLabel;
        pendingPhotoSourceSave.person = pendingPhotoSourceSave.person || !!changes.person;
        pendingPhotoSourceSave.personLabel = pendingPhotoSourceSave.personLabel || !!changes.personLabel;
      }
      clearTimeout(photoSourceApplyTimer);
      photoSourceApplyTimer = setTimeout(applyPhotoSourceInputs, delayMs == null ? 600 : delayMs);
    }
    function refreshPersonRemoveButtons() {
      Array.prototype.forEach.call(personIdList.querySelectorAll(".person-id-remove"), function (btn) {
        btn.disabled = personInputs.length <= 1;
      });
    }
    function addPersonIdRow(value, labelValue) {
      var row = el("div", "photo-id-row");
      var fields = el("div", "photo-id-fields");
      var personInput = input("text", value || "", "Paste person ID from Immich URL", MAX_PHOTO_ID_FIELD_LENGTH);
      var personLabelInput = input("text", labelValue || "", "Who is it?", MAX_PHOTO_ID_FIELD_LENGTH);
      var removeBtn = el("button", "btn btn-secondary btn-icon person-id-remove");
      removeBtn.type = "button";
      removeBtn.innerHTML = removeIdIcon;
      removeBtn.title = "Remove person ID";
      removeBtn.setAttribute("aria-label", "Remove person ID");
      removeBtn.onclick = function () {
        if (personInputs.length <= 1) {
          personInput.value = "";
          personLabelInput.value = "";
          schedulePhotoSourceApply(0, { person: true, personLabel: true });
          return;
        }
        var removeIndex = personInputs.indexOf(personInput);
        personInputs.splice(removeIndex, 1);
        personLabelInputs.splice(removeIndex, 1);
        row.parentNode.removeChild(row);
        refreshPersonRemoveButtons();
        schedulePhotoSourceApply(0, { person: true, personLabel: true });
      };
      personInput.oninput = function () {
        schedulePhotoSourceApply(null, { person: true, personLabel: true });
      };
      personLabelInput.oninput = function () {
        schedulePhotoSourceApply(null, { personLabel: true });
      };
      fields.appendChild(personInput);
      fields.appendChild(personLabelInput);
      row.appendChild(fields);
      row.appendChild(removeBtn);
      personIdList.appendChild(row);
      personInputs.push(personInput);
      personLabelInputs.push(personLabelInput);
      refreshPersonRemoveButtons();
    }
    var personIds = splitPhotoIdList(S.person_ids);
    var personLabels = parsePhotoLabelList(S.person_labels);
    for (var personIndex = 0; personIndex < Math.max(personIds.length, personLabels.length, 1); personIndex++) {
      addPersonIdRow(personIds[personIndex] || "", personLabels[personIndex] || "");
    }
    var addPersonRow = el("div", "photo-id-actions");
    var addPersonBtn = el("button", "btn btn-secondary");
    addPersonBtn.type = "button";
    addPersonBtn.textContent = "Add a person";
    addPersonBtn.title = "Add a person";
    addPersonBtn.setAttribute("aria-label", "Add a person");
    addPersonBtn.onclick = function () {
      addPersonIdRow("", "");
      personInputs[personInputs.length - 1].focus();
    };
    addPersonRow.appendChild(addPersonBtn);
    personField.appendChild(personIdList);
    personField.appendChild(addPersonRow);
    personField.appendChild(personError);
    personField.style.display = S.photo_source === "Person" ? "" : "none";

    fSrc.appendChild(srcSel);
    srcBody.appendChild(fSrc);
    srcBody.appendChild(albumField);
    srcBody.appendChild(personField);

    immichWrap.appendChild(makeCollapsibleCard("Photo Source", srcBody, true));

    // Display Settings
    var photoBody = el("div");

    var fPairToggle = field("");
    var pairTr = el("div", "toggle-row");
    pairTr.innerHTML = "<span>Portrait Pairing</span>";
    var pairTog = el("div", S.portrait_pairing ? "toggle on" : "toggle");
    pairTog.onclick = function () {
      S.portrait_pairing = !S.portrait_pairing;
      pairTog.className = S.portrait_pairing ? "toggle on" : "toggle";
      post(endpoints.portrait_pairing + (S.portrait_pairing ? "/turn_on" : "/turn_off"));
    };
    pairTr.appendChild(pairTog);
    fPairToggle.appendChild(pairTr);
    photoBody.appendChild(fPairToggle);

    var fPhotoOrientation = field("Photo Orientation");
    fPhotoOrientation.appendChild(
      selectFromOptions(S.photo_orientation_options, S.photo_orientation, function (v) {
        S.photo_orientation = v;
        post(endpoints.photo_orientation + "/set", { option: v });
      })
    );
    photoBody.appendChild(fPhotoOrientation);

    var fDisplayMode = field("Display Mode");
    fDisplayMode.appendChild(
      selectFromOptions(S.display_mode_options, S.display_mode, function (v) {
        S.display_mode = v;
        post(endpoints.display_mode + "/set", { option: v });
      })
    );
    photoBody.appendChild(fDisplayMode);

    // Advanced Filters
    var DATE_RE = /^\d{4}-\d{2}-\d{2}$/;
    function isValidDate(s) {
      if (!DATE_RE.test(s)) return false;
      var parts = s.split("-");
      var d = new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]));
      return d.getFullYear() === Number(parts[0]) && d.getMonth() === Number(parts[1]) - 1 && d.getDate() === Number(parts[2]);
    }
    function isFilterActive(enabled) {
      return !!enabled;
    }
    var filterBadge = makeBadge(isFilterActive(S.date_filter_enabled));
    var filterBody = el("div");
    var filterApplyTimer = null;
    var fFilterToggle = field("");
    var filterTr = el("div", "toggle-row");
    filterTr.innerHTML = "<span>Filter by Date</span>";
    var filterTog = el("div", S.date_filter_enabled ? "toggle on" : "toggle");
    var filterDetails = el("div");
    filterDetails.style.display = S.date_filter_enabled ? "" : "none";
    filterTog.onclick = function () {
      S.date_filter_enabled = !S.date_filter_enabled;
      filterTog.className = S.date_filter_enabled ? "toggle on" : "toggle";
      filterDetails.style.display = S.date_filter_enabled ? "" : "none";
      filterBadge.className = "on-badge" + (isFilterActive(S.date_filter_enabled) ? " active" : "");
      scheduleFilterApply();
    };
    filterTr.appendChild(filterTog);
    fFilterToggle.appendChild(filterTr);
    filterBody.appendChild(fFilterToggle);

    var fFilterMode = field("Mode");
    var modeVal = S.date_filter_mode;
    var modeSegment = segmentedControl(S.date_filter_mode_options, modeVal, function (v) {
      modeVal = v;
      updateFilterModeDisplay(v);
      scheduleFilterApply();
    }, function (v) {
      return v === "Relative Range" ? "Relative" : "Fixed";
    });
    fFilterMode.appendChild(modeSegment);
    filterDetails.appendChild(fFilterMode);

    var fixedWrap = el("div");
    var fDateFrom = field("From");
    var dateFromInput = document.createElement("input");
    dateFromInput.type = "date";
    dateFromInput.value = S.date_from || "";
    dateFromInput.placeholder = "YYYY-MM-DD";
    var dateFromError = el("div", "field-error");
    fDateFrom.appendChild(dateFromInput);
    fDateFrom.appendChild(dateFromError);
    fixedWrap.appendChild(fDateFrom);

    var fDateTo = field("Until");
    var dateToInput = document.createElement("input");
    dateToInput.type = "date";
    dateToInput.value = S.date_to || "";
    dateToInput.placeholder = "YYYY-MM-DD";
    var dateToError = el("div", "field-error");
    fDateTo.appendChild(dateToInput);
    fDateTo.appendChild(dateToError);
    fixedWrap.appendChild(fDateTo);
    filterDetails.appendChild(fixedWrap);

    var relativeWrap = el("div", "filter-relative-row");
    var fRelativeAmount = field("Last");
    var relativeAmountInput = document.createElement("input");
    relativeAmountInput.type = "number";
    relativeAmountInput.min = "1";
    relativeAmountInput.max = "120";
    relativeAmountInput.step = "1";
    relativeAmountInput.value = String(S.relative_amount || 1);
    var relativeAmountError = el("div", "field-error");
    fRelativeAmount.appendChild(relativeAmountInput);
    fRelativeAmount.appendChild(relativeAmountError);
    relativeWrap.appendChild(fRelativeAmount);

    var fRelativeUnit = field("Unit");
    var relativeUnitSelect = selectFromOptions(S.relative_unit_options, S.relative_unit, function () {
      scheduleFilterApply();
    });
    fRelativeUnit.appendChild(relativeUnitSelect);
    relativeWrap.appendChild(fRelativeUnit);
    filterDetails.appendChild(relativeWrap);

    function updateFilterModeDisplay(mode) {
      fixedWrap.style.display = mode === "Relative Range" ? "none" : "";
      relativeWrap.style.display = mode === "Relative Range" ? "" : "none";
    }
    updateFilterModeDisplay(S.date_filter_mode);

    var filterError = el("div", "field-error");
    filterDetails.appendChild(filterError);

    dateFromInput.onchange = scheduleFilterApply;
    dateToInput.onchange = scheduleFilterApply;
    relativeAmountInput.onchange = scheduleFilterApply;

    function readFilterValues() {
      dateFromError.textContent = "";
      dateToError.textContent = "";
      relativeAmountError.textContent = "";
      filterError.textContent = "";
      var fromVal = dateFromInput.value.trim();
      var toVal = dateToInput.value.trim();
      var amountVal = Math.round(Number(relativeAmountInput.value));
      var unitVal = relativeUnitSelect.value;
      if (S.date_filter_enabled && modeVal === "Fixed Range" && fromVal && !isValidDate(fromVal)) {
        dateFromError.textContent = "Invalid date — use YYYY-MM-DD";
        return null;
      }
      if (S.date_filter_enabled && modeVal === "Fixed Range" && toVal && !isValidDate(toVal)) {
        dateToError.textContent = "Invalid date — use YYYY-MM-DD";
        return null;
      }
      if (S.date_filter_enabled && modeVal === "Fixed Range" && fromVal && toVal && fromVal > toVal) {
        filterError.textContent = "From must not be after Until";
        return null;
      }
      if (S.date_filter_enabled && modeVal === "Relative Range" && (!amountVal || amountVal < 1 || amountVal > 120)) {
        relativeAmountError.textContent = "Enter a whole number from 1 to 120";
        return null;
      }
      return { from: fromVal, to: toVal, amount: amountVal || 1, unit: unitVal };
    }

    function applyFilterSettings() {
      var vals = readFilterValues();
      if (!vals) return;
      S.date_filter_mode = modeVal;
      S.date_from = vals.from;
      S.date_to = vals.to;
      S.relative_amount = vals.amount;
      S.relative_unit = vals.unit;
      filterBadge.className = "on-badge" + (isFilterActive(S.date_filter_enabled) ? " active" : "");
      Promise.all([
        post(endpoints.date_filter_enabled + (S.date_filter_enabled ? "/turn_on" : "/turn_off")),
        post(endpoints.date_filter_mode + "/set", { option: modeVal }),
        post(endpoints.date_from + "/set", { value: vals.from }),
        post(endpoints.date_to + "/set", { value: vals.to }),
        post(endpoints.relative_amount + "/set", { value: vals.amount }),
        post(endpoints.relative_unit + "/set", { option: vals.unit })
      ]).then(function () {
        post(eid("button", "Apply Photo Source") + "/press");
      });
    }

    function scheduleFilterApply() {
      clearTimeout(filterApplyTimer);
      filterApplyTimer = setTimeout(applyFilterSettings, 300);
    }

    filterBody.appendChild(filterDetails);
    immichWrap.appendChild(makeCollapsibleCard("Advanced Filters", filterBody, true, filterBadge));
    immichWrap.appendChild(makeCollapsibleCard("Display Settings", photoBody, true));

    immichApp.appendChild(immichWrap);

    // Device Metadata
    function metadataIsActive() {
      return S.photo_metadata_enabled &&
        (S.photo_metadata_time_enabled || S.photo_metadata_location_enabled);
    }
    var metadataBadge = makeBadge(metadataIsActive());
    var metadataBody = el("div");
    var metadataDetails = el("div");

    function refreshMetadataDetails() {
      metadataDetails.style.display = S.photo_metadata_enabled ? "" : "none";
      metadataBadge.className = "on-badge" + (metadataIsActive() ? " active" : "");
    }

    var fMetadataToggle = field("");
    var metadataTr = el("div", "toggle-row");
    metadataTr.innerHTML = "<span>Show Photo Metadata</span>";
    var metadataTog = el("div", S.photo_metadata_enabled ? "toggle on" : "toggle");
    metadataTog.onclick = function () {
      S.photo_metadata_enabled = !S.photo_metadata_enabled;
      metadataTog.className = S.photo_metadata_enabled ? "toggle on" : "toggle";
      refreshMetadataDetails();
      post(endpoints.photo_metadata_enabled + (S.photo_metadata_enabled ? "/turn_on" : "/turn_off"));
    };
    metadataTr.appendChild(metadataTog);
    fMetadataToggle.appendChild(metadataTr);
    metadataBody.appendChild(fMetadataToggle);

    var fMetadataTime = field("");
    var metadataTimeTr = el("div", "toggle-row");
    metadataTimeTr.innerHTML = "<span>Display Photo Time</span>";
    var metadataTimeTog = el("div", S.photo_metadata_time_enabled ? "toggle on" : "toggle");
    metadataTimeTog.onclick = function () {
      S.photo_metadata_time_enabled = !S.photo_metadata_time_enabled;
      metadataTimeTog.className = S.photo_metadata_time_enabled ? "toggle on" : "toggle";
      refreshMetadataDetails();
      post(endpoints.photo_metadata_time_enabled + (S.photo_metadata_time_enabled ? "/turn_on" : "/turn_off"));
    };
    metadataTimeTr.appendChild(metadataTimeTog);
    fMetadataTime.appendChild(metadataTimeTr);
    metadataDetails.appendChild(fMetadataTime);

    var fMetadataLocation = field("");
    var metadataLocationTr = el("div", "toggle-row");
    metadataLocationTr.innerHTML = "<span>Display Photo Location</span>";
    var metadataLocationTog = el("div", S.photo_metadata_location_enabled ? "toggle on" : "toggle");
    metadataLocationTog.onclick = function () {
      S.photo_metadata_location_enabled = !S.photo_metadata_location_enabled;
      metadataLocationTog.className = S.photo_metadata_location_enabled ? "toggle on" : "toggle";
      refreshMetadataDetails();
      post(endpoints.photo_metadata_location_enabled + (S.photo_metadata_location_enabled ? "/turn_on" : "/turn_off"));
    };
    metadataLocationTr.appendChild(metadataLocationTog);
    fMetadataLocation.appendChild(metadataLocationTr);
    metadataDetails.appendChild(fMetadataLocation);

    metadataBody.appendChild(metadataDetails);
    refreshMetadataDetails();
    wrap.appendChild(makeCollapsibleCard("Device Metadata", metadataBody, true, metadataBadge));

    // Screen Brightness
    var dnDetails = el("div");

    var fDayBrt = field("Daytime Brightness");
    var rwDay = el("div", "range-wrap");
    var daySlider = document.createElement("input");
    daySlider.type = "range";
    daySlider.min = 10;
    daySlider.max = 100;
    daySlider.step = 5;
    daySlider.value = S.brightness_day;
    var dayVal = el("span", "range-val");
    dayVal.textContent = Math.round(S.brightness_day) + "%";
    daySlider.oninput = function () {
      dayVal.textContent = daySlider.value + "%";
    };
    daySlider.onchange = function () {
      post(endpoints.brightness_day + "/set", { value: daySlider.value });
    };
    rwDay.appendChild(daySlider);
    rwDay.appendChild(dayVal);
    fDayBrt.appendChild(rwDay);
    dnDetails.appendChild(fDayBrt);

    var fNightBrt = field("Nighttime Brightness");
    var rwNight = el("div", "range-wrap");
    var nightSlider = document.createElement("input");
    nightSlider.type = "range";
    nightSlider.min = 10;
    nightSlider.max = 100;
    nightSlider.step = 5;
    nightSlider.value = S.brightness_night;
    var nightVal = el("span", "range-val");
    nightVal.textContent = Math.round(S.brightness_night) + "%";
    nightSlider.oninput = function () {
      nightVal.textContent = nightSlider.value + "%";
    };
    nightSlider.onchange = function () {
      post(endpoints.brightness_night + "/set", { value: nightSlider.value });
    };
    rwNight.appendChild(nightSlider);
    rwNight.appendChild(nightVal);
    fNightBrt.appendChild(rwNight);
    dnDetails.appendChild(fNightBrt);

    var fSunInfo = el("div", "field sun-info");
    fSunInfo.id = "sun-info";
    function updateSunInfo() {
      updateSunInfoElement(fSunInfo);
    }
    updateSunInfo();
    dnDetails.appendChild(fSunInfo);

    wrap.appendChild(makeCollapsibleCard("Screen Brightness", dnDetails, true));

    // Screen Tone
    var toneBadge = makeBadge(S.base_tone_enabled || S.warm_tones_enabled);
    var warmBody = el("div");

    var fBaseToneToggle = field("");
    var baseTr = el("div", "toggle-row");
    baseTr.innerHTML = "<span>Screen Tone Adjustment</span>";
    var baseTog = el("div", S.base_tone_enabled ? "toggle on" : "toggle");
    var baseDetails = el("div");
    baseDetails.style.display = S.base_tone_enabled ? "" : "none";

    baseTog.onclick = function () {
      S.base_tone_enabled = !S.base_tone_enabled;
      baseTog.className = S.base_tone_enabled ? "toggle on" : "toggle";
      baseDetails.style.display = S.base_tone_enabled ? "" : "none";
      toneBadge.className = "on-badge" + ((S.base_tone_enabled || S.warm_tones_enabled) ? " active" : "");
      post(endpoints.base_tone_enabled + (S.base_tone_enabled ? "/turn_on" : "/turn_off"));
    };
    baseTr.appendChild(baseTog);
    fBaseToneToggle.appendChild(baseTr);
    fBaseToneToggle.style.marginBottom = "8px";
    warmBody.appendChild(fBaseToneToggle);

    var fBaseTone = field("");
    var rwBase = el("div", "range-wrap");
    var baseLabelL = el("span", "range-label");
    baseLabelL.textContent = "Cooler";
    var baseSlider = document.createElement("input");
    baseSlider.type = "range";
    baseSlider.min = 0;
    baseSlider.max = 100;
    baseSlider.step = 5;
    baseSlider.value = S.base_tone;
    baseSlider.onchange = function () {
      post(endpoints.base_tone + "/set", { value: baseSlider.value });
    };
    var baseLabelR = el("span", "range-label");
    baseLabelR.textContent = "Warmer";
    rwBase.appendChild(baseLabelL);
    rwBase.appendChild(baseSlider);
    rwBase.appendChild(baseLabelR);
    fBaseTone.appendChild(rwBase);
    baseDetails.appendChild(fBaseTone);
    baseDetails.style.marginBottom = "28px";
    warmBody.appendChild(baseDetails);

    var fWarmToggle = field("");
    var warmTr = el("div", "toggle-row");
    warmTr.innerHTML = "<span>Night Tone Adjustment</span>";
    var warmTog = el("div", S.warm_tones_enabled ? "toggle on" : "toggle");
    var nightDetails = el("div");
    nightDetails.style.display = S.warm_tones_enabled ? "" : "none";

    warmTog.onclick = function () {
      S.warm_tones_enabled = !S.warm_tones_enabled;
      warmTog.className = S.warm_tones_enabled ? "toggle on" : "toggle";
      nightDetails.style.display = S.warm_tones_enabled ? "" : "none";
      toneBadge.className = "on-badge" + ((S.base_tone_enabled || S.warm_tones_enabled) ? " active" : "");
      post(endpoints.warm_tones_enabled + (S.warm_tones_enabled ? "/turn_on" : "/turn_off"));
    };
    warmTr.appendChild(warmTog);
    fWarmToggle.appendChild(warmTr);
    fWarmToggle.style.marginBottom = "8px";
    warmBody.appendChild(fWarmToggle);

    var fWarmInt = field("");
    var rwWarm = el("div", "range-wrap");
    var warmLabelL = el("span", "range-label");
    warmLabelL.textContent = "Cooler";
    var warmSlider = document.createElement("input");
    warmSlider.type = "range";
    warmSlider.min = 10;
    warmSlider.max = 100;
    warmSlider.step = 5;
    warmSlider.value = S.warm_tone_intensity;
    warmSlider.onchange = function () {
      post(endpoints.warm_tone_intensity + "/set", { value: warmSlider.value });
    };
    var warmLabelR = el("span", "range-label");
    warmLabelR.textContent = "Warmer";
    rwWarm.appendChild(warmLabelL);
    rwWarm.appendChild(warmSlider);
    rwWarm.appendChild(warmLabelR);
    fWarmInt.appendChild(rwWarm);
    nightDetails.appendChild(fWarmInt);

    var fOverride = field("");
    var overTr = el("div", "toggle-row");
    overTr.innerHTML = "<span>Turn on until sunrise</span>";
    var overTog = el("div", S.warm_tone_override ? "toggle on" : "toggle");
    overTog.onclick = function () {
      S.warm_tone_override = !S.warm_tone_override;
      overTog.className = S.warm_tone_override ? "toggle on" : "toggle";
      post(endpoints.warm_tone_override + (S.warm_tone_override ? "/turn_on" : "/turn_off"));
    };
    overTr.appendChild(overTog);
    fOverride.appendChild(overTr);
    nightDetails.appendChild(fOverride);

    warmBody.appendChild(nightDetails);
    wrap.appendChild(makeCollapsibleCard("Screen Tone", warmBody, true, toneBadge));

    // Schedule
    var schedBadge = makeBadge(S.schedule_enabled);
    var schedBody = el("div");
    var fSchedToggle = field("");
    var schedTr = el("div", "toggle-row");
    schedTr.innerHTML = "<span>Schedule Screen Off</span>";
    var schedTog = el("div", S.schedule_enabled ? "toggle on" : "toggle");
    var schedDetails = el("div");
    schedDetails.style.display = S.schedule_enabled ? "" : "none";

    schedTog.onclick = function () {
      S.schedule_enabled = !S.schedule_enabled;
      schedTog.className = S.schedule_enabled ? "toggle on" : "toggle";
      schedDetails.style.display = S.schedule_enabled ? "" : "none";
      schedBadge.className = "on-badge" + (S.schedule_enabled ? " active" : "");
      post(endpoints.schedule_enabled + (S.schedule_enabled ? "/turn_on" : "/turn_off"));
    };
    schedTr.appendChild(schedTog);
    fSchedToggle.appendChild(schedTr);
    schedBody.appendChild(fSchedToggle);

    var fOnTime = field("On Time");
    var onSel = document.createElement("select");
    onSel.className = "select";
    for (var h = 0; h < 24; h++) {
      var o = document.createElement("option");
      o.value = h;
      o.textContent = formatHour(h);
      if (h === Math.round(S.schedule_on_hour)) o.selected = true;
      onSel.appendChild(o);
    }
    onSel.onchange = function () {
      S.schedule_on_hour = parseInt(onSel.value);
      post(endpoints.schedule_on_hour + "/set", { value: onSel.value });
    };
    fOnTime.appendChild(onSel);
    schedDetails.appendChild(fOnTime);

    var fOffTime = field("Off Time");
    var offSel = document.createElement("select");
    offSel.className = "select";
    for (var h2 = 0; h2 < 24; h2++) {
      var o2 = document.createElement("option");
      o2.value = h2;
      o2.textContent = formatHour(h2);
      if (h2 === Math.round(S.schedule_off_hour)) o2.selected = true;
      offSel.appendChild(o2);
    }
    offSel.onchange = function () {
      S.schedule_off_hour = parseInt(offSel.value);
      post(endpoints.schedule_off_hour + "/set", { value: offSel.value });
    };
    fOffTime.appendChild(offSel);
    schedDetails.appendChild(fOffTime);

    var fWakeTimeout = field("When Woken, Idle Time To Screen Off");
    fWakeTimeout.appendChild(
      selectFromOptions([10, 30, 60, 120, 300, 600, 1800, 3600], normalizeScheduleWakeTimeout(S.schedule_wake_timeout), function (v) {
        S.schedule_wake_timeout = normalizeScheduleWakeTimeout(v);
        postScheduleWakeTimeout(S.schedule_wake_timeout);
      }, formatDurationSeconds)
    );
    schedDetails.appendChild(fWakeTimeout);

    schedBody.appendChild(schedDetails);
    wrap.appendChild(makeCollapsibleCard("Night Schedule", schedBody, true, schedBadge));

    // Rotation
    var rotationBody = el("div");
    var fRotation = field("Rotation");
    fRotation.appendChild(
      selectFromOptions(S.screen_rotation_options, S.screen_rotation, function (v) {
        S.screen_rotation = v;
        post(endpoints.screen_rotation + "/set", { option: v });
      }, function (v) {
        return v + " degrees";
      })
    );
    rotationBody.appendChild(fRotation);
    wrap.appendChild(makeCollapsibleCard("Rotation", rotationBody, true));

    // Clock
    var clockBadge = makeBadge(S.show_clock);
    var clkBody = el("div");
    var f5 = field("");
    var tr = el("div", "toggle-row");
    tr.innerHTML = "<span>Show Clock</span>";
    var tog = el("div", S.show_clock ? "toggle on" : "toggle");
    tog.onclick = function () {
      S.show_clock = !S.show_clock;
      tog.className = S.show_clock ? "toggle on" : "toggle";
      clockBadge.className = "on-badge" + (S.show_clock ? " active" : "");
      post(
        endpoints.show_clock + (S.show_clock ? "/turn_on" : "/turn_off")
      );
    };
    tr.appendChild(tog);
    f5.appendChild(tr);
    clkBody.appendChild(f5);

    var f6 = field("Format");
    f6.appendChild(
      selectFromOptions(S.clock_options, S.clock_format, function (v) {
        S.clock_format = v;
        post(endpoints.clock_format + "/set", { option: v });
      })
    );
    clkBody.appendChild(f6);

    var f7 = field("Timezone");
    f7.appendChild(
      timezoneSelect(S.tz_options, S.timezone, function (v) {
        post(endpoints.timezone + "/set", { option: v });
        S.timezone = v;
      })
    );
    clkBody.appendChild(f7);
    wrap.appendChild(makeCollapsibleCard("Clock", clkBody, true, clockBadge));

    // Firmware
    var fwBody = el("div", "fw-body");
    var versionRow = el("div", "field fw-row");
    var versionLabel = el("span", "fw-label");
    versionLabel.innerHTML = '<span style="color:var(--text2)">Installed</span> ' +
      esc(displayVersion(S.firmware || S.installed_version, "Dev"));
    var checkBtn = el("button", "btn btn-secondary btn-sm");
    checkBtn.textContent = "Check for Update";
    var statusMsg = el("span", "fw-status");
    versionRow.appendChild(versionLabel);
    var checkWrap = el("div");
    checkWrap.className = "check-wrap";
    checkWrap.appendChild(statusMsg);
    checkWrap.appendChild(checkBtn);
    versionRow.appendChild(checkWrap);
    var versionBlock = el("div");
    versionBlock.appendChild(versionRow);
    fwBody.appendChild(versionBlock);

    var updatesSection = el("div", "fw-updates");
    var updateRow = el("div");
    updatesSection.appendChild(updateRow);
    var betaRow = el("div");
    updatesSection.appendChild(betaRow);
    fwBody.appendChild(updatesSection);

    function renderUpdateRow() {
      updateRow.innerHTML = "";
      if (!S.update_available) return;
      var row = el("div", "field fw-row");
      var label = el("span", "fw-label");
      label.innerHTML = '<span style="color:var(--text2)">Stable</span> ' + esc(S.latest_version);
      var installBtn = el("button", "btn btn-primary btn-sm");
      installBtn.textContent = "Install";
      installBtn.onclick = function () {
        installBtn.disabled = true;
        installBtn.textContent = "Installing\u2026";
        post(endpoints.update + "/install");
      };
      row.appendChild(label);
      row.appendChild(installBtn);
      updateRow.appendChild(row);
    }

    function renderBetaRow() {
      betaRow.innerHTML = "";
      if (!S.beta_available) return;
      var row = el("div", "field fw-row");
      var label = el("span", "fw-label");
      label.innerHTML = '<span style="color:var(--text2)">Pre-release</span> ' + esc(S.beta_version);
      var betaBtn = el("button", "btn btn-secondary btn-sm");
      betaBtn.textContent = "Install";
      betaBtn.onclick = function () {
        betaBtn.disabled = true;
        betaBtn.textContent = "Installing\u2026";
        post(endpoints.update_beta + "/install");
      };
      row.appendChild(label);
      row.appendChild(betaBtn);
      betaRow.appendChild(row);
    }

    renderUpdateRow();
    renderBetaRow();

    checkBtn.onclick = function () {
      checkBtn.disabled = true;
      checkBtn.textContent = "Checking\u2026";
      statusMsg.textContent = "";
      post(eid("button", "Firmware: Check for Update") + "/press")
        .then(function () {
          return new Promise(function (r) {
            setTimeout(r, 4000);
          });
        })
        .then(function () {
          return safeGet(endpoints.update);
        })
        .then(function (data) {
          checkBtn.disabled = false;
          checkBtn.textContent = "Check for Update";
          var hasUpdate = data && data.value &&
            (data.current_version
              ? data.current_version !== data.latest_version
              : data.state === "UPDATE AVAILABLE");
          if (hasUpdate) {
            S.update_available = true;
            S.latest_version = data.latest_version || data.value;
            renderUpdateRow();
          }
          return safeGet(endpoints.update_beta);
        })
        .then(function (betaData) {
          if (betaData && (betaData.latest_version || betaData.value)) {
            S.beta_version = betaData.latest_version || betaData.value;
            S.beta_available = betaData.current_version
              ? betaData.latest_version !== betaData.current_version
              : betaData.state === "UPDATE AVAILABLE";
          }
          renderBetaRow();
          if (!S.update_available && !S.beta_available) {
            statusMsg.textContent = "Up to date";
            statusMsg.style.color = "var(--success)";
          }
        });
    };

    var autoUpdateOptions = ["Disabled"].concat(S.update_freq_options);
    var currentAutoUpdate = S.auto_update ? S.update_frequency : "Disabled";
    var freqField = field("Auto updates");
    freqField.appendChild(
      selectFromOptions(autoUpdateOptions, currentAutoUpdate, function (v) {
        if (v === "Disabled") {
          S.auto_update = false;
          post(endpoints.auto_update + "/turn_off");
        } else {
          S.auto_update = true;
          S.update_frequency = v;
          post(endpoints.auto_update + "/turn_on");
          post(endpoints.update_frequency + "/set", { option: v });
        }
      })
    );
    fwBody.appendChild(freqField);

    wrap.appendChild(makeCollapsibleCard("Firmware", fwBody, true));

    wrap.appendChild(makeBackupCard());

    app.appendChild(wrap);

    if (S.firmware) {
      var verLine = el("div", "version");
      verLine.textContent = displayVersion(S.firmware);
      app.appendChild(verLine);
    }
  }

  // --- SSE live updates (after render) ---

  function handleLiveEvent(d) {
    if (!d || !d.id) return;
    var id = d.id;
    if (id === "light/Screen: Backlight") {
      S.backlight_on = d.state === "ON";
      if (d.brightness != null) {
        S.brightness = Math.round((d.brightness / 255) * 100);
        S.brightness_current = S.brightness;
      }
    } else if (id === "switch/Clock: Show") {
      S.show_clock = d.state === "ON" || d.value === true;
    } else if (id === "text_sensor/Screen: Sunrise") {
      S.sunrise = d.value || d.state || "";
      updateSunInfoElement(document.getElementById("sun-info"));
    } else if (id === "text_sensor/Screen: Sunset") {
      S.sunset = d.value || d.state || "";
      updateSunInfoElement(document.getElementById("sun-info"));
    } else if (ENTITY_STATE_MAP[id] && ENTITY_STATE_MAP[id].key.indexOf("photo_metadata_") === 0) {
      if (!isEditingSetting()) renderSettings();
    } else if (ENTITY_STATE_MAP[id] && ENTITY_STATE_MAP[id].key.indexOf("schedule_") === 0) {
      if (!isEditingSetting()) renderSettings();
    }
  }

  function updateSunInfoElement(el) {
    if (!el) return;
    if (!S.sunrise && !S.sunset) {
      el.style.display = "none";
      return;
    }
    el.style.display = "";
    var t = "";
    if (S.sunrise) t += "Sunrise: " + esc(S.sunrise);
    if (S.sunrise && S.sunset) t += " \u00a0/\u00a0 ";
    if (S.sunset) t += "Sunset: " + esc(S.sunset);
    el.innerHTML = t;
  }

  // --- Hour formatting ---

  function formatHour(h) {
    h = Math.round(h);
    if (h === 0) return "12:00 AM";
    if (h < 12) return h + ":00 AM";
    if (h === 12) return "12:00 PM";
    return (h - 12) + ":00 PM";
  }

  function normalizeScheduleWakeTimeout(value) {
    var seconds = Math.round(Number(value));
    if (!seconds) seconds = 60;
    if (seconds < 10) seconds = 10;
    if (seconds > 3600) seconds = 3600;
    return seconds;
  }

  function formatDurationSeconds(seconds) {
    seconds = normalizeScheduleWakeTimeout(seconds);
    if (seconds < 60) return seconds + " seconds";
    if (seconds % 60 === 0) {
      var minutes = seconds / 60;
      return minutes + (minutes === 1 ? " minute" : " minutes");
    }
    return seconds + " seconds";
  }

  // --- Select helpers ---

  function selectFromOptions(options, current, onChange, optionDisplayFn) {
    var display = optionDisplayFn || function (o) { return o; };
    var sel = document.createElement("select");
    sel.className = "select";
    options.forEach(function (o) {
      var opt = document.createElement("option");
      opt.value = o;
      opt.textContent = display(o);
      if (o === current) opt.selected = true;
      sel.appendChild(opt);
    });
    sel.onchange = function () {
      onChange(sel.value);
    };
    return sel;
  }

  function segmentedControl(options, current, onChange, optionDisplayFn) {
    var display = optionDisplayFn || function (o) { return o; };
    var seg = el("div", "segment");
    function setActive(value) {
      Array.prototype.forEach.call(seg.children, function (button) {
        var active = button.dataset.value === value;
        button.className = active ? "active" : "";
        button.setAttribute("aria-pressed", active ? "true" : "false");
      });
    }
    options.forEach(function (o) {
      var btn = document.createElement("button");
      btn.type = "button";
      btn.dataset.value = o;
      btn.textContent = display(o);
      btn.setAttribute("aria-pressed", o === current ? "true" : "false");
      btn.onclick = function () {
        setActive(o);
        onChange(o);
      };
      seg.appendChild(btn);
    });
    setActive(current);
    return seg;
  }

  function timezoneSelect(options, current, onChange) {
    return selectFromOptions(options, current, onChange, function (o) {
      return o.replace(/_/g, " ");
    });
  }

  // --- Helpers ---

  function el(tag, cls) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    return e;
  }

  function makeBadge(isActive) {
    var badge = el("span", "on-badge" + (isActive ? " active" : ""));
    badge.textContent = "On";
    return badge;
  }

  function makeCollapsibleCard(title, bodyElement, defaultCollapsed, badgeEl) {
    var card = el("div", "card");
    var header = el("div", "card-header");
    var h3 = document.createElement("h3");
    h3.textContent = title;
    var rightWrap = el("div", "card-header-right");
    if (badgeEl) rightWrap.appendChild(badgeEl);
    var chevron = el("span", "card-chevron");
    chevron.innerHTML = "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M6 9l6 6 6-6\"/></svg>";
    rightWrap.appendChild(chevron);
    header.appendChild(h3);
    header.appendChild(rightWrap);
    var body = el("div", "card-body");
    body.appendChild(bodyElement);
    card.appendChild(header);
    card.appendChild(body);
    if (defaultCollapsed) card.classList.add("collapsed");
    header.onclick = function () { card.classList.toggle("collapsed"); };
    return card;
  }

  function makeBackupCard() {
    var backupBody = el("div");
    var backupRow = el("div", "backup-row");
    var exportBtn = el("button", "btn btn-secondary");
    exportBtn.innerHTML = "Export";
    exportBtn.onclick = exportConfig;
    var importBtn = el("button", "btn btn-secondary");
    importBtn.innerHTML = "Import";
    importBtn.onclick = importConfig;
    backupRow.appendChild(exportBtn);
    backupRow.appendChild(importBtn);
    backupBody.appendChild(backupRow);
    return makeCollapsibleCard("Backup", backupBody, true);
  }

  function makeImportSettingsCard() {
    var importBody = el("div");
    var importBtn = el("button", "btn btn-secondary btn-block");
    importBtn.innerHTML = "Import Settings";
    importBtn.onclick = importConfig;
    importBody.appendChild(importBtn);
    return makeCollapsibleCard("Import Settings", importBody, false);
  }

  function field(labelText) {
    var f = el("div", "field");
    if (labelText) {
      var l = document.createElement("label");
      l.textContent = labelText;
      f.appendChild(l);
    }
    return f;
  }

  function input(type, value, placeholder, maxLength) {
    var i = document.createElement("input");
    i.type = type;
    i.value = value || "";
    if (placeholder) i.placeholder = placeholder;
    if (maxLength != null && maxLength > 0) i.maxLength = maxLength;
    return i;
  }

  function esc(s) {
    var d = document.createElement("div");
    d.textContent = s;
    return d.innerHTML;
  }

  // --- Banner ---

  var bannerTimer = null;
  function showBanner(msg, type) {
    if (!els.banner) return;
    els.banner.textContent = msg;
    els.banner.className = "banner banner-" + (type || "success");
    els.banner.style.display = "";
    clearTimeout(bannerTimer);
    bannerTimer = setTimeout(function () { els.banner.style.display = "none"; }, 5000);
  }

  // --- Import / Export ---

  function exportConfig() {
    var data = {
      version: 1,
      exported_at: new Date().toISOString(),
      connection: {
        immich_url: S.immich_url,
        api_key: S.api_key
      },
      photos: {
        source: S.photo_source,
        album_ids: S.album_ids,
        album_labels: S.album_labels,
        person_ids: S.person_ids,
        person_labels: S.person_labels,
        date_filter_enabled: S.date_filter_enabled,
        date_filter_mode: S.date_filter_mode,
        date_from: S.date_from,
        date_to: S.date_to,
        relative_amount: S.relative_amount,
        relative_unit: S.relative_unit,
        orientation: S.photo_orientation,
        portrait_pairing: S.portrait_pairing,
        display_mode: S.display_mode
      },
      frequency: {
        interval: S.interval,
        conn_timeout: S.conn_timeout
      },
      clock: {
        show: S.show_clock,
        format: S.clock_format,
        timezone: S.timezone
      },
      screen: {
        brightness_day: S.brightness_day,
        brightness_night: S.brightness_night,
        schedule_enabled: S.schedule_enabled,
        schedule_on_hour: S.schedule_on_hour,
        schedule_off_hour: S.schedule_off_hour,
        schedule_wake_timeout: normalizeScheduleWakeTimeout(S.schedule_wake_timeout),
        base_tone_enabled: S.base_tone_enabled,
        base_tone: S.base_tone,
        warm_tones_enabled: S.warm_tones_enabled,
        warm_tone_intensity: S.warm_tone_intensity,
        warm_tone_override: S.warm_tone_override,
        rotation: S.screen_rotation
      }
    };

    var json = JSON.stringify(data, null, 2);
    var blob = new Blob([json], { type: "application/json" });
    var url = URL.createObjectURL(blob);
    var now = new Date();
    var name = "espframe-config-" +
      now.getFullYear() + "-" +
      String(now.getMonth() + 1).padStart(2, "0") + "-" +
      String(now.getDate()).padStart(2, "0") + ".json";
    var a = document.createElement("a");
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }

  function importConfig() {
    var fileInput = document.createElement("input");
    fileInput.type = "file";
    fileInput.accept = ".json";
    fileInput.style.display = "none";

    fileInput.addEventListener("change", function () {
      if (!fileInput.files || !fileInput.files[0]) return;
      var reader = new FileReader();
      reader.onload = function () {
        var data;
        try { data = JSON.parse(reader.result); } catch (_) {
          showBanner("Invalid file \u2014 could not parse JSON", "error");
          return;
        }

        if (!data.version) {
          showBanner("Invalid config file \u2014 missing version", "error");
          return;
        }

        var c = data.connection || {};
        var p = data.photos || {};
        var f = data.frequency || {};
        var clk = data.clock || {};
        var scr = data.screen || {};

        if (c.immich_url !== undefined) {
          S.immich_url = normalizeImmichUrl(c.immich_url);
          postTextValueSet(endpoints.immich_url + "/set", S.immich_url, true);
        }
        if (c.api_key !== undefined) {
          S.api_key = c.api_key;
          postTextValueSet(endpoints.api_key + "/set", c.api_key, true);
        }

        if (p.source !== undefined) {
          S.photo_source = p.source;
          post(endpoints.photo_source + "/set", { option: p.source });
        }
        if (p.album_ids !== undefined) {
          var importAlbum = String(p.album_ids).trim();
          if (photoIdFieldTooLong(importAlbum)) {
            showBanner("Album IDs exceed 255 characters - not imported", "error");
          } else if (!isValidUuidList(importAlbum)) {
            showBanner("Import skipped invalid album IDs", "error");
          } else {
            S.album_ids = importAlbum;
            postTextValueSet(endpoints.album_ids + "/set", importAlbum);
          }
        }
        if (p.album_labels !== undefined) {
          var importAlbumLabels = String(p.album_labels).trim();
          if (photoLabelFieldTooLong(importAlbumLabels)) {
            showBanner("Album labels exceed 255 characters - not imported", "error");
          } else {
            S.album_labels = importAlbumLabels;
            postTextValueSet(endpoints.album_labels + "/set", importAlbumLabels);
          }
        }
        if (p.person_ids !== undefined) {
          var importPerson = String(p.person_ids).trim();
          if (photoIdFieldTooLong(importPerson)) {
            showBanner("Person IDs exceed 255 characters - not imported", "error");
          } else if (!isValidUuidList(importPerson)) {
            showBanner("Import skipped invalid person IDs", "error");
          } else {
            S.person_ids = importPerson;
            postTextValueSet(endpoints.person_ids + "/set", importPerson);
          }
        }
        if (p.person_labels !== undefined) {
          var importPersonLabels = String(p.person_labels).trim();
          if (photoLabelFieldTooLong(importPersonLabels)) {
            showBanner("Person labels exceed 255 characters - not imported", "error");
          } else {
            S.person_labels = importPersonLabels;
            postTextValueSet(endpoints.person_labels + "/set", importPersonLabels);
          }
        }
        if (p.portrait_pairing !== undefined) {
          S.portrait_pairing = p.portrait_pairing;
          post(endpoints.portrait_pairing + (p.portrait_pairing ? "/turn_on" : "/turn_off"));
        }
        if (p.display_mode !== undefined) {
          S.display_mode = p.display_mode;
          post(endpoints.display_mode + "/set", { option: p.display_mode });
        }
        if (p.orientation !== undefined) {
          S.photo_orientation = p.orientation;
          post(endpoints.photo_orientation + "/set", { option: p.orientation });
        }
        if (p.date_filter_enabled !== undefined) {
          S.date_filter_enabled = p.date_filter_enabled;
          post(endpoints.date_filter_enabled + (p.date_filter_enabled ? "/turn_on" : "/turn_off"));
        }
        if (p.date_filter_mode !== undefined) {
          S.date_filter_mode = p.date_filter_mode;
          post(endpoints.date_filter_mode + "/set", { option: p.date_filter_mode });
        }
        if (p.date_from !== undefined) {
          S.date_from = p.date_from;
          post(endpoints.date_from + "/set", { value: p.date_from });
        }
        if (p.date_to !== undefined) {
          S.date_to = p.date_to;
          post(endpoints.date_to + "/set", { value: p.date_to });
        }
        if (p.relative_amount !== undefined) {
          S.relative_amount = p.relative_amount;
          post(endpoints.relative_amount + "/set", { value: p.relative_amount });
        }
        if (p.relative_unit !== undefined) {
          S.relative_unit = p.relative_unit;
          post(endpoints.relative_unit + "/set", { option: p.relative_unit });
        }

        if (f.interval !== undefined) {
          S.interval = f.interval;
          post(endpoints.interval + "/set", { option: f.interval });
        }
        if (f.conn_timeout !== undefined) {
          S.conn_timeout = f.conn_timeout;
          post(endpoints.conn_timeout + "/set", { option: f.conn_timeout });
        }

        if (clk.show !== undefined) {
          S.show_clock = clk.show;
          post(endpoints.show_clock + (clk.show ? "/turn_on" : "/turn_off"));
        }
        if (clk.format !== undefined) {
          S.clock_format = clk.format;
          post(endpoints.clock_format + "/set", { option: clk.format });
        }
        if (clk.timezone !== undefined) {
          S.timezone = clk.timezone;
          post(endpoints.timezone + "/set", { option: clk.timezone });
        }

        if (scr.brightness_day !== undefined) {
          S.brightness_day = scr.brightness_day;
          post(endpoints.brightness_day + "/set", { value: scr.brightness_day });
        }
        if (scr.brightness_night !== undefined) {
          S.brightness_night = scr.brightness_night;
          post(endpoints.brightness_night + "/set", { value: scr.brightness_night });
        }

        if (scr.schedule_enabled !== undefined) {
          S.schedule_enabled = scr.schedule_enabled;
          post(endpoints.schedule_enabled + (scr.schedule_enabled ? "/turn_on" : "/turn_off"));
        }
        if (scr.schedule_on_hour !== undefined) {
          S.schedule_on_hour = scr.schedule_on_hour;
          post(endpoints.schedule_on_hour + "/set", { value: scr.schedule_on_hour });
        }
        if (scr.schedule_off_hour !== undefined) {
          S.schedule_off_hour = scr.schedule_off_hour;
          post(endpoints.schedule_off_hour + "/set", { value: scr.schedule_off_hour });
        }
        if (scr.schedule_wake_timeout !== undefined) {
          S.schedule_wake_timeout = normalizeScheduleWakeTimeout(scr.schedule_wake_timeout);
          postScheduleWakeTimeout(S.schedule_wake_timeout);
        }

        if (scr.base_tone_enabled !== undefined) {
          S.base_tone_enabled = scr.base_tone_enabled;
          post(endpoints.base_tone_enabled + (scr.base_tone_enabled ? "/turn_on" : "/turn_off"));
        }
        if (scr.base_tone !== undefined) {
          S.base_tone = scr.base_tone;
          post(endpoints.base_tone + "/set", { value: scr.base_tone });
        }
        if (scr.warm_tones_enabled !== undefined) {
          S.warm_tones_enabled = scr.warm_tones_enabled;
          post(endpoints.warm_tones_enabled + (scr.warm_tones_enabled ? "/turn_on" : "/turn_off"));
        }
        if (scr.warm_tone_intensity !== undefined) {
          S.warm_tone_intensity = scr.warm_tone_intensity;
          post(endpoints.warm_tone_intensity + "/set", { value: scr.warm_tone_intensity });
        }
        if (scr.warm_tone_override !== undefined) {
          S.warm_tone_override = scr.warm_tone_override;
          post(endpoints.warm_tone_override + (scr.warm_tone_override ? "/turn_on" : "/turn_off"));
        }
        if (scr.rotation !== undefined) {
          var importedRotation = String(scr.rotation);
          if (S.screen_rotation_options.indexOf(importedRotation) !== -1) {
            S.screen_rotation = importedRotation;
            post(endpoints.screen_rotation + "/set", { option: S.screen_rotation });
          }
        }

        showBanner("Settings imported successfully", "success");
        renderSettings();
      };
      reader.readAsText(fileInput.files[0]);
    });

    document.body.appendChild(fileInput);
    fileInput.click();
    document.body.removeChild(fileInput);
  }

  // --- Init ---

  buildUI();
  initSSE();
})();
