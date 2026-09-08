  var evtSource = null;
  var rendered = false;
  var renderTimer = null;
  var renderAttemptInFlight = false;
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
  var ANSI_RE = /\x1b\[[\d;]*m/g;

  function appendLog(msg, lvl) {
    if (!els.logOutput) return;
    var line = document.createElement("div");
    line.className = "sp-log-line";

    var ansiClass = "";
    var m = msg.match(/\x1b\[([\d;]+)m/);
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
    var overflow = els.logOutput.childNodes.length - WEB_UI_LOGS_RETAINED_LINES;
    if (overflow > 0) {
      for (var i = 0; i < overflow; i++)
        els.logOutput.removeChild(els.logOutput.firstChild);
    }
    if (atBottom) els.logOutput.scrollTop = els.logOutput.scrollHeight;
  }

  // Entity id -> state key mapping; optional optionsKey and default.
  var ENTITY_STATE_MAP = {};

  function registerManualStateEntities() {
    if (!MANUAL_ENTITIES) return;
    (Array.isArray(MANUAL_STATE_KEYS) ? MANUAL_STATE_KEYS : []).forEach(function (key) {
      var manualSpec = MANUAL_ENTITIES[key];
      if (!manualSpec || typeof manualSpec.entity !== "string") return;
      ENTITY_STATE_MAP[manualSpec.entity] = { key: key };
    });
  }

  function registerStaticEntities() {
    if (!STATIC_ENTITIES) return;
    Object.keys(STATIC_ENTITIES).forEach(function (key) {
      var staticSpec = STATIC_ENTITIES[key];
      if (!staticSpec || typeof staticSpec.entity !== "string") return;
      var stateSpec: EntityStateSpec = { key: key };
      if (staticSpec.default !== undefined) stateSpec.default = staticSpec.default;
      if (staticSpec.optionsKey) stateSpec.optionsKey = staticSpec.optionsKey;
      if (staticSpec.boolFromState) stateSpec.boolFromState = true;
      if (staticSpec.number) stateSpec.number = true;
      ENTITY_STATE_MAP[staticSpec.entity] = stateSpec;
    });
  }

  function registerProductSettingEntities() {
    if (!PRODUCT_SETTINGS) return;
    Object.keys(PRODUCT_SETTINGS).forEach(function (key) {
      var productSpec = PRODUCT_SETTINGS[key];
      if (!productSpec || typeof productSpec.entity !== "string") return;
      var stateSpec: EntityStateSpec = { key: key, default: productSpec.default };
      if (productSpec.domain === "switch") stateSpec.boolFromState = true;
      if (productSpec.domain === "number") stateSpec.number = true;
      ENTITY_STATE_MAP[productSpec.entity] = stateSpec;
    });
  }

  registerManualStateEntities();
  registerStaticEntities();
  registerProductSettingEntities();

  function registerEntityAliases() {
    if (!ENTITY_ALIASES) return;
    Object.keys(ENTITY_ALIASES).forEach(function (key) {
      var aliases = ENTITY_ALIASES[key];
      if (!Array.isArray(aliases)) return;
      aliases.forEach(function (aliasSpec) {
        if (!aliasSpec || typeof aliasSpec.entity !== "string") return;
        var stateSpec: EntityStateSpec = { key: key };
        if (aliasSpec.default !== undefined) stateSpec.default = aliasSpec.default;
        if (aliasSpec.optionsKey) stateSpec.optionsKey = aliasSpec.optionsKey;
        if (aliasSpec.boolFromState) stateSpec.boolFromState = true;
        if (aliasSpec.number) stateSpec.number = true;
        ENTITY_STATE_MAP[aliasSpec.entity] = stateSpec;
      });
    });
  }

  registerEntityAliases();

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
    var spec = ENTITY_STATE_MAP[id];
    if (!spec) return;
    var v = d.value != null ? d.value : d.state;
    var received;
    if (spec.boolFromState) {
      received = v === true || v === "ON";
    } else if (spec.number) {
      received = v != null ? Math.round(Number(v)) : (spec.default !== undefined ? spec.default : 0);
    } else {
      received = v !== undefined && v !== null ? String(v) : (spec.default !== undefined ? spec.default : "");
    }
    if (spec.key === "timezone") received = normalizeTimezoneOption(received);
    if (spec.key && spec.key.indexOf("ntp_server_") === 0) received = normalizeNtpServer(received);
    if (spec.optionsKey && d.option && d.option.length) S[spec.optionsKey] = d.option;
    if (spec.key === "photo_metadata_date_format" &&
        received !== "Relative Date" && received !== "Date Taken") {
      settingSaves.receive("photo_metadata_date_taken_format", normalizeDateTakenFormat(received));
      received = "Date Taken";
    }
    if (spec.key === "photo_metadata_date_taken_format") {
      received = normalizeDateTakenFormat(received);
    }
    settingSaves.receive(spec.key, received);
  }

  function collectState(d) {
    applyEntityToState(d);
  }

  // Generated from product metadata plus status-only fields; KEY_TO_ENTITY_ID derived from ENTITY_STATE_MAP.
  var INITIAL_FETCH_KEYS = __ESPFRAME_INITIAL_FETCH_KEYS__;
  function getEntityIdForStateKey(key) {
    var productSpec = PRODUCT_SETTINGS && PRODUCT_SETTINGS[key];
    if (productSpec && typeof productSpec.entity === "string") return productSpec.entity;
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
    return getConfigurationSnapshot().then(function (snapshot) {
      applyConfigurationSnapshot(snapshot);
    }).catch(function (error) {
      if (!isConfigurationApiUnavailable(error)) throw error;
      return fetchLegacyDeviceSettingsState();
    });
  }

  function fetchLegacyDeviceSettingsState() {
    // The configuration API's key list intentionally omits connection secrets.
    // Legacy devices still need these two reads to distinguish setup from an
    // already-configured frame when SSE is unavailable.
    var legacyKeys = ["immich_url", "api_key"].concat(INITIAL_FETCH_KEYS);
    var urls = legacyKeys.map(function (k) {
      if (!endpoints[k]) {
        console.error("Missing endpoint for startup setting:", k);
        return Promise.resolve(null);
      }
      return safeGet(endpoints[k]);
    });
    return Promise.all(urls).then(function (res) {
      for (var i = 0; i < res.length; i++) {
        var data = res[i];
        if (!data) continue;
        applyEntityToState({
          id: getEntityIdForStateKey(legacyKeys[i]),
          value: data.value,
          state: data.state,
          option: data.option
        });
      }
    });
  }

  function withStartupTimeout(promise, timeoutMs) {
    return Promise.race([
      promise,
      new Promise(function (_, reject) {
        setTimeout(function () { reject(new Error("startup_settings_timeout")); }, timeoutMs);
      })
    ]);
  }

  function isEditingSetting() {
    var active = document.activeElement;
    return !!(active && els.root && els.root.contains(active) &&
      active.matches("input,select,textarea,button"));
  }

  var deferredRenderControl = null;

  function resumeSettingsRenderAfterBlur() {
    deferredRenderControl = null;
    if (renderTimer) return;
    // Let focus and the next control's click settle before replacing the UI.
    renderTimer = setTimeout(function () {
      renderTimer = null;
      renderSettingsAfterEditing();
    }, 0);
  }

  function renderSettingsAfterEditing() {
    var active = isEditingSetting() ? document.activeElement : null;
    if (deferredRenderControl && deferredRenderControl !== active) {
      deferredRenderControl.removeEventListener("blur", resumeSettingsRenderAfterBlur);
      deferredRenderControl = null;
    }
    if (!active) return renderSettings();
    if (deferredRenderControl === active) return;
    deferredRenderControl = active;
    active.addEventListener("blur", resumeSettingsRenderAfterBlur, { once: true });
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
    renderSettings();
  }

  function tryRender() {
    if (rendered || renderAttemptInFlight) return;
    if (renderTimer) {
      clearTimeout(renderTimer);
      renderTimer = null;
    }
    renderAttemptInFlight = true;
    // Wait for the complete snapshot (or legacy settings) before showing cards.
    // SSE can deliver the connection URL long before the remaining settings.
    withStartupTimeout(fetchDeviceSettingsState(), 4000).then(function () {
      renderAttemptInFlight = false;
      if (rendered) return;
      if (S.immich_url) {
        showConfiguredSettings();
      } else {
        rendered = true;
        renderWizard();
      }
    }).catch(function () {
      renderAttemptInFlight = false;
      if (S.immich_url) showConfiguredSettings();
      else scheduleTryRender(1000);
    });
  }

  function initSSE() {
    try {
      evtSource = new EventSource("/events");

      evtSource.addEventListener("state", function (e) {
        try {
          var d = JSON.parse(e.data);
          // Normalise the entity key at ingress. The web server sends name_id
          // ("domain/Friendly Name" - the shape ENTITY_STATE_MAP and the REST
          // endpoints use) alongside a legacy id ("domain-object_id"). ESPHome
          // 2026.8.0 drops name_id and switches id to the name form, so preferring
          // name_id and otherwise leaving id alone is correct either side of that.
          if (d && d.name_id) d.id = d.name_id;
          var spec = d && ENTITY_STATE_MAP[d.id];
          var previousValue = spec ? S[spec.key] : undefined;
          var previousOptions = spec && spec.optionsKey ? JSON.stringify(S[spec.optionsKey]) : "";
          collectState(d);
          var changed = !spec || previousValue !== S[spec.key] ||
            (spec.optionsKey && previousOptions !== JSON.stringify(S[spec.optionsKey]));
          if (rendered && changed) handleLiveEvent(d);
        } catch (_) {}

        if (!rendered) {
          scheduleTryRender(250);
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

      evtSource.onopen = function () {
        handleFirmwareReconnect();
      };
    } catch (_) {
      tryRender();
    }

    scheduleTryRender(250);
    setTimeout(function () {
      if (!rendered) tryRender();
    }, 5000);
  }

  // --- Wizard ---
