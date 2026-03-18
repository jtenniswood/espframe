(function () {
  "use strict";

  var TIMEZONES = [
    "Pacific/Midway (GMT-11)","Pacific/Pago_Pago (GMT-11)","Pacific/Honolulu (GMT-10)",
    "America/Adak (GMT-10)","America/Anchorage (GMT-9)","America/Juneau (GMT-9)",
    "America/Los_Angeles (GMT-8)","America/Vancouver (GMT-8)","America/Tijuana (GMT-8)",
    "America/Denver (GMT-7)","America/Phoenix (GMT-7)","America/Edmonton (GMT-7)",
    "America/Boise (GMT-7)","America/Chicago (GMT-6)","America/Mexico_City (GMT-6)",
    "America/Winnipeg (GMT-6)","America/Guatemala (GMT-6)","America/Costa_Rica (GMT-6)",
    "America/New_York (GMT-5)","America/Toronto (GMT-5)","America/Detroit (GMT-5)",
    "America/Havana (GMT-5)","America/Bogota (GMT-5)","America/Lima (GMT-5)",
    "America/Jamaica (GMT-5)","America/Panama (GMT-5)","America/Halifax (GMT-4)",
    "America/Caracas (GMT-4)","America/Santiago (GMT-4)","America/La_Paz (GMT-4)",
    "America/Manaus (GMT-4)","America/Barbados (GMT-4)","America/Puerto_Rico (GMT-4)",
    "America/Santo_Domingo (GMT-4)","America/St_Johns (GMT-3:30)",
    "America/Sao_Paulo (GMT-3)","America/Argentina/Buenos_Aires (GMT-3)",
    "America/Montevideo (GMT-3)","America/Paramaribo (GMT-3)",
    "Atlantic/South_Georgia (GMT-2)","Atlantic/Azores (GMT-1)","Atlantic/Cape_Verde (GMT-1)",
    "UTC (GMT+0)","Europe/London (GMT+0)","Europe/Dublin (GMT+0)","Europe/Lisbon (GMT+0)",
    "Africa/Casablanca (GMT+1)","Africa/Accra (GMT+0)","Atlantic/Reykjavik (GMT+0)",
    "Europe/Paris (GMT+1)","Europe/Berlin (GMT+1)","Europe/Rome (GMT+1)",
    "Europe/Madrid (GMT+1)","Europe/Amsterdam (GMT+1)","Europe/Brussels (GMT+1)",
    "Europe/Vienna (GMT+1)","Europe/Zurich (GMT+1)","Europe/Stockholm (GMT+1)",
    "Europe/Oslo (GMT+1)","Europe/Copenhagen (GMT+1)","Europe/Warsaw (GMT+1)",
    "Europe/Prague (GMT+1)","Europe/Budapest (GMT+1)","Europe/Belgrade (GMT+1)",
    "Africa/Lagos (GMT+1)","Africa/Tunis (GMT+1)","Africa/Cairo (GMT+2)",
    "Europe/Athens (GMT+2)","Europe/Bucharest (GMT+2)","Europe/Helsinki (GMT+2)",
    "Europe/Kyiv (GMT+2)","Europe/Istanbul (GMT+3)","Africa/Johannesburg (GMT+2)",
    "Africa/Nairobi (GMT+3)","Asia/Jerusalem (GMT+2)","Asia/Amman (GMT+3)",
    "Asia/Beirut (GMT+2)","Europe/Moscow (GMT+3)","Asia/Baghdad (GMT+3)",
    "Asia/Riyadh (GMT+3)","Asia/Kuwait (GMT+3)","Asia/Qatar (GMT+3)",
    "Africa/Addis_Ababa (GMT+3)","Asia/Tehran (GMT+3:30)","Asia/Dubai (GMT+4)",
    "Asia/Muscat (GMT+4)","Asia/Baku (GMT+4)","Asia/Tbilisi (GMT+4)",
    "Indian/Mauritius (GMT+4)","Asia/Kabul (GMT+4:30)","Asia/Karachi (GMT+5)",
    "Asia/Tashkent (GMT+5)","Asia/Yekaterinburg (GMT+5)","Asia/Kolkata (GMT+5:30)",
    "Asia/Colombo (GMT+5:30)","Asia/Kathmandu (GMT+5:45)","Asia/Dhaka (GMT+6)",
    "Asia/Almaty (GMT+6)","Asia/Rangoon (GMT+6:30)","Asia/Bangkok (GMT+7)",
    "Asia/Jakarta (GMT+7)","Asia/Ho_Chi_Minh (GMT+7)","Asia/Singapore (GMT+8)",
    "Asia/Kuala_Lumpur (GMT+8)","Asia/Shanghai (GMT+8)","Asia/Hong_Kong (GMT+8)",
    "Asia/Taipei (GMT+8)","Asia/Manila (GMT+8)","Australia/Perth (GMT+8)",
    "Asia/Tokyo (GMT+9)","Asia/Seoul (GMT+9)","Asia/Pyongyang (GMT+9)",
    "Australia/Adelaide (GMT+9:30)","Australia/Darwin (GMT+9:30)",
    "Australia/Sydney (GMT+10)","Australia/Melbourne (GMT+10)","Australia/Brisbane (GMT+10)",
    "Australia/Hobart (GMT+10)","Pacific/Guam (GMT+10)","Pacific/Port_Moresby (GMT+10)",
    "Asia/Vladivostok (GMT+10)","Pacific/Noumea (GMT+11)","Pacific/Norfolk (GMT+11)",
    "Asia/Magadan (GMT+11)","Pacific/Auckland (GMT+12)","Pacific/Fiji (GMT+12)",
    "Pacific/Chatham (GMT+12:45)","Pacific/Tongatapu (GMT+13)","Pacific/Apia (GMT+13)",
    "Pacific/Kiritimati (GMT+14)"
  ];

  var S = {
    clock_options: ["24 Hour", "12 Hour"],
    tz_options: TIMEZONES,
    interval: "15 seconds",
    interval_options: [
      "10 seconds", "15 seconds", "20 seconds", "30 seconds", "45 seconds",
      "1 minute", "2 minutes", "3 minutes", "5 minutes", "10 minutes"
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
    beta_opt_in: localStorage.getItem("beta_opt_in") === "true",
    auto_update: true,
    update_frequency: "Daily",
    update_freq_options: ["Hourly", "Daily", "Weekly"],
    schedule_enabled: false,
    schedule_on_hour: 6,
    schedule_off_hour: 23,
    brightness_current: 0,
    brightness_day: 100,
    brightness_night: 75,
    sunrise: "",
    sunset: "",
    photo_source: "All Photos",
    photo_source_options: ["All Photos", "Favorites", "Album", "Person", "Memories"],
    album_ids: "",
    person_ids: "",
  };

  var app = document.getElementById("app");
  if (!app) {
    app = document.createElement("div");
    app.id = "app";
    document.body.appendChild(app);
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
    backlight: eid("light", "Screen: Backlight"),
    show_clock: eid("switch", "Clock: Show"),
    firmware: eid("text_sensor", "Firmware: Version"),
    update: eid("update", "Firmware: Update"),
    update_beta: eid("update", "Firmware: Update Beta"),
    auto_update: eid("switch", "Firmware: Auto Update"),
    update_frequency: eid("select", "Firmware: Update Frequency"),
    schedule_enabled: eid("switch", "Screen: Schedule"),
    schedule_on_hour: eid("number", "Screen: Schedule On"),
    schedule_off_hour: eid("number", "Screen: Schedule Off"),
    brightness_day: eid("number", "Screen: Daytime Brightness"),
    brightness_night: eid("number", "Screen: Nighttime Brightness"),
    sunrise: eid("text_sensor", "Screen: Sunrise"),
    sunset: eid("text_sensor", "Screen: Sunset"),
    photo_source: eid("select", "Photos: Source"),
    album_ids: eid("text", "Photos: Album IDs"),
    person_ids: eid("text", "Photos: Person IDs"),
  };

  function post(url, params) {
    var qs = params
      ? "?" +
        Object.keys(params)
          .map(function (k) {
            return k + "=" + encodeURIComponent(params[k]);
          })
          .join("&")
      : "";
    return fetch(url + qs, { method: "POST" }).catch(function () {});
  }

  var UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
  function isValidUuidList(str) {
    var s = str.trim();
    if (!s) return true;
    return s.split(",").every(function (id) { return UUID_RE.test(id.trim()); });
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

  // --- SSE-based init ---

  var evtSource = null;
  var rendered = false;
  var renderTimer = null;
  var logLines = [];
  var logPreRef = null;
  var logMaxLines = 200;

  function logLevelClass(line) {
    if (/\[E\]/.test(line)) return "log-error";
    if (/\[W\]/.test(line)) return "log-warning";
    if (/\[I\]/.test(line)) return "log-info";
    return "";
  }

  function collectState(d) {
    if (!d || !d.id) return;
    var id = d.id;
    if (id === "text/Connection: Server URL") {
      S.immich_url = d.value || d.state || "";
    } else if (id === "text/Connection: API Key") {
      S.api_key = d.value || d.state || "";
    } else if (id === "select/Clock: Format") {
      S.clock_format = d.value || "24 Hour";
      if (d.option && d.option.length) S.clock_options = d.option;
    } else if (id === "select/Clock: Timezone") {
      S.timezone = d.value || "";
      if (d.option && d.option.length) S.tz_options = d.option;
    } else if (id === "select/Photos: Slideshow Interval") {
      S.interval = d.value || "15 seconds";
      if (d.option && d.option.length) S.interval_options = d.option;
    } else if (id === "light/Screen: Backlight") {
      S.backlight_on = d.state === "ON";
      if (d.brightness != null) {
        S.brightness = Math.round((d.brightness / 255) * 100);
        S.brightness_current = S.brightness;
      }
    } else if (id === "switch/Clock: Show") {
      S.show_clock = d.value === true || d.state === "ON";
    } else if (id === "text_sensor/Firmware: Version") {
      S.firmware = d.value || d.state || "";
    } else if (id === "update/Firmware: Update") {
      S.installed_version = d.current_version || "";
      S.latest_version = d.latest_version || "";
      S.update_available =
        S.installed_version &&
        S.latest_version &&
        S.installed_version !== S.latest_version;
    } else if (id === "update/Firmware: Update Beta") {
      S.beta_version = d.latest_version || "";
      S.beta_available =
        S.beta_version &&
        d.current_version &&
        S.beta_version !== d.current_version;
    } else if (id === "switch/Firmware: Auto Update") {
      S.auto_update = d.value === true || d.state === "ON";
    } else if (id === "select/Firmware: Update Frequency") {
      S.update_frequency = d.value || "Daily";
      if (d.option && d.option.length) S.update_freq_options = d.option;
    } else if (id === "switch/Screen: Schedule") {
      S.schedule_enabled = d.value === true || d.state === "ON";
    } else if (id === "number/Screen: Schedule On") {
      S.schedule_on_hour = d.value != null ? d.value : 6;
    } else if (id === "number/Screen: Schedule Off") {
      S.schedule_off_hour = d.value != null ? d.value : 23;
    } else if (id === "number/Screen: Daytime Brightness") {
      S.brightness_day = d.value != null ? d.value : 100;
    } else if (id === "number/Screen: Nighttime Brightness") {
      S.brightness_night = d.value != null ? d.value : 75;
    } else if (id === "text_sensor/Screen: Sunrise") {
      S.sunrise = d.value || d.state || "";
    } else if (id === "text_sensor/Screen: Sunset") {
      S.sunset = d.value || d.state || "";
    } else if (id === "select/Photos: Source") {
      S.photo_source = d.value || "All Photos";
      if (d.option && d.option.length) S.photo_source_options = d.option;
    } else if (id === "text/Photos: Album IDs") {
      S.album_ids = d.value || d.state || "";
    } else if (id === "text/Photos: Person IDs") {
      S.person_ids = d.value || d.state || "";
    }
  }

  function fetchDeviceSettingsState() {
    return Promise.all([
      safeGet(endpoints.photo_source),
      safeGet(endpoints.album_ids),
      safeGet(endpoints.person_ids),
      safeGet(endpoints.interval),
      safeGet(endpoints.schedule_enabled),
      safeGet(endpoints.schedule_on_hour),
      safeGet(endpoints.schedule_off_hour),
      safeGet(endpoints.sunrise),
      safeGet(endpoints.sunset)
    ]).then(function (res) {
      if (res[0] && (res[0].value != null || res[0].state != null))
        S.photo_source = res[0].value || res[0].state || "All Photos";
      if (res[0] && res[0].option && res[0].option.length)
        S.photo_source_options = res[0].option;
      if (res[1]) S.album_ids = res[1].value || res[1].state || "";
      if (res[2]) S.person_ids = res[2].value || res[2].state || "";
      if (res[3] && (res[3].value != null || res[3].state != null))
        S.interval = res[3].value || res[3].state || "15 seconds";
      if (res[3] && res[3].option && res[3].option.length)
        S.interval_options = res[3].option;
      if (res[4])
        S.schedule_enabled = res[4].value === true || res[4].state === "ON";
      if (res[5] && res[5].value != null)
        S.schedule_on_hour = Math.round(Number(res[5].value));
      if (res[6] && res[6].value != null)
        S.schedule_off_hour = Math.round(Number(res[6].value));
      if (res[7]) S.sunrise = res[7].value || res[7].state || "";
      if (res[8]) S.sunset = res[8].value || res[8].state || "";
    });
  }

  function tryRender() {
    if (rendered) return;
    rendered = true;
    if (S.immich_url) {
      fetchDeviceSettingsState().then(renderSettings);
      return;
    }
    Promise.all([
      safeGet(endpoints.immich_url),
      safeGet(endpoints.api_key)
    ]).then(function (res) {
      if (res[0]) S.immich_url = res[0].value || res[0].state || "";
      if (res[1]) S.api_key = res[1].value || res[1].state || "";
      if (S.immich_url) {
        fetchDeviceSettingsState().then(renderSettings);
      } else {
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
          clearTimeout(renderTimer);
          renderTimer = setTimeout(tryRender, 500);
        }
      });

      evtSource.addEventListener("log", function (e) {
        var line = e.data;
        logLines.push(line);
        if (logLines.length > logMaxLines) logLines.shift();
        if (logPreRef) {
          var parts = [];
          for (var i = 0; i < logLines.length; i++) {
            var ln = logLines[i];
            var cls = logLevelClass(ln);
            parts.push(cls ? '<span class="' + cls + '">' + esc(ln) + '</span>' : '<span>' + esc(ln) + '</span>');
          }
          logPreRef.innerHTML = parts.join("\n");
          logPreRef.scrollTop = logPreRef.scrollHeight;
        }
      });

      evtSource.onerror = function () {
        if (!rendered) {
          clearTimeout(renderTimer);
          renderTimer = setTimeout(tryRender, 1000);
        }
      };

      evtSource.onopen = function () {};
    } catch (_) {
      tryRender();
    }

    setTimeout(function () {
      if (!rendered) tryRender();
    }, 5000);
  }

  // --- Wizard ---

  function renderWizard() {
    var step = 1;
    app.innerHTML = "";
    var wrap = el("div", "fade-in");
    wrap.innerHTML =
      '<h1>Espframe</h1><p class="subtitle">Let\'s connect your photo frame</p>';
    var steps = el("div", "wizard-steps");
    var s1 = el("div", "step active");
    var s2 = el("div", "step");
    steps.appendChild(s1);
    steps.appendChild(s2);
    wrap.appendChild(steps);

    var body = el("div");
    wrap.appendChild(body);
    app.appendChild(wrap);

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
      var urlInput = input("url", S.immich_url, "http://192.168.0.1:2283");
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
        var u = urlInput.value.trim();
        var k = keyInput.value.trim();
        if (!u || !k) return;
        nextBtn.disabled = true;
        nextBtn.textContent = "Saving\u2026";
        post(endpoints.immich_url + "/set", { value: u })
          .then(function () {
            return new Promise(function (r) { setTimeout(r, 500); });
          })
          .then(function () {
            return post(endpoints.api_key + "/set", { value: k });
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
      card.innerHTML = "<h3>Clock & Timezone</h3>";

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

  // --- Settings ---

  function renderSettings() {
    app.innerHTML = "";
    var wrap = el("div", "fade-in");
    wrap.innerHTML = '<h1>Espframe</h1><h2>Settings</h2>';

    // Connection
    var connBody = el("div");
    var connStatus = el("div", "status");
    connStatus.id = "conn-status";
    connStatus.style.marginBottom = "12px";

    function showSaved(msg) {
      connStatus.innerHTML = '<span class="dot green"></span> ' + (msg || "Saved");
      clearTimeout(connStatus._t);
      connStatus._t = setTimeout(function () {
        connStatus.textContent = "";
      }, 3000);
    }

    var f1 = field("Immich Server URL");
    var urlInput = input("url", S.immich_url, "http://192.168.0.1:2283");
    urlInput.onchange = function () {
      post(endpoints.immich_url + "/set", { value: urlInput.value.trim() });
      showSaved("URL saved");
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
      mask.style.cssText =
        "flex:1;padding:10px 12px;background:var(--surface2);border:1px solid var(--border);" +
        "border-radius:6px;color:var(--text2);font-size:.9rem;letter-spacing:2px";
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
        post(endpoints.api_key + "/set", { value: v }).then(function () {
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

    connBody.appendChild(connStatus);
    wrap.appendChild(makeCollapsibleCard("Connection", connBody, true));

    // Photo Source
    var srcBody = el("div");
    var fSrc = field("Source");
    var srcSel = selectFromOptions(S.photo_source_options, S.photo_source, function (v) {
      S.photo_source = v;
      albumField.style.display = v === "Album" ? "" : "none";
      personField.style.display = v === "Person" ? "" : "none";
    });

    var albumField = field("Album IDs");
    var albumInput = input("text", S.album_ids, "Paste album IDs, comma-separated");
    var albumError = el("div", "field-error");
    var albumHint = el("div");
    albumHint.style.cssText = "font-size:.75rem;color:var(--text2);margin-top:4px";
    albumHint.textContent = "Find IDs in your Immich server URL bar";
    albumField.appendChild(albumInput);
    albumField.appendChild(albumError);
    albumField.appendChild(albumHint);
    albumField.style.display = S.photo_source === "Album" ? "" : "none";

    var personField = field("Person IDs");
    var personInput = input("text", S.person_ids, "Paste person IDs, comma-separated");
    var personError = el("div", "field-error");
    var personHint = el("div");
    personHint.style.cssText = "font-size:.75rem;color:var(--text2);margin-top:4px";
    personHint.textContent = "Find IDs in your Immich server URL bar";
    personField.appendChild(personInput);
    personField.appendChild(personError);
    personField.appendChild(personHint);
    personField.style.display = S.photo_source === "Person" ? "" : "none";

    var applyBtn = el("button", "btn btn-primary btn-block");
    applyBtn.textContent = "Apply";
    applyBtn.style.marginTop = "12px";
    applyBtn.onclick = function () {
      albumError.textContent = "";
      personError.textContent = "";
      var src_val = srcSel.value;
      if (src_val === "Album" && !isValidUuidList(albumInput.value)) {
        albumError.textContent = "Invalid UUID format";
        return;
      }
      if (src_val === "Person" && !isValidUuidList(personInput.value)) {
        personError.textContent = "Invalid UUID format";
        return;
      }
      applyBtn.disabled = true;
      applyBtn.textContent = "Applying\u2026";
      post(endpoints.photo_source + "/set", { option: src_val });
      post(endpoints.album_ids + "/set", { value: albumInput.value.trim() });
      post(endpoints.person_ids + "/set", { value: personInput.value.trim() });
      post(eid("button", "Apply Photo Source") + "/press").then(function () {
        applyBtn.textContent = "Applied";
        setTimeout(function () {
          applyBtn.disabled = false;
          applyBtn.textContent = "Apply";
        }, 2000);
      });
    };

    fSrc.appendChild(srcSel);
    srcBody.appendChild(fSrc);
    srcBody.appendChild(albumField);
    srcBody.appendChild(personField);
    srcBody.appendChild(applyBtn);
    wrap.appendChild(makeCollapsibleCard("Photo Source", srcBody, false));

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
    wrap.appendChild(makeCollapsibleCard("Frequency", dispBody, true));

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

    // Schedule
    var schedBody = el("div");
    var fSchedToggle = field("");
    var schedTr = el("div", "toggle-row");
    schedTr.innerHTML = "<span>Enable Schedule</span>";
    var schedTog = el("div", S.schedule_enabled ? "toggle on" : "toggle");
    var schedDetails = el("div");
    schedDetails.style.display = S.schedule_enabled ? "" : "none";

    schedTog.onclick = function () {
      S.schedule_enabled = !S.schedule_enabled;
      schedTog.className = S.schedule_enabled ? "toggle on" : "toggle";
      schedDetails.style.display = S.schedule_enabled ? "" : "none";
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

    schedBody.appendChild(schedDetails);
    wrap.appendChild(makeCollapsibleCard("Screen Schedule", schedBody, true));

    // Clock
    var clkBody = el("div");
    var f5 = field("");
    var tr = el("div", "toggle-row");
    tr.innerHTML = "<span>Show Clock</span>";
    var tog = el("div", S.show_clock ? "toggle on" : "toggle");
    tog.onclick = function () {
      S.show_clock = !S.show_clock;
      tog.className = S.show_clock ? "toggle on" : "toggle";
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
    wrap.appendChild(makeCollapsibleCard("Clock", clkBody, true));

    // Firmware
    var fwBody = el("div");
    var fwRowStyle = "display:flex;align-items:center;justify-content:space-between;min-height:36px";
    var versionRow = el("div", "field");
    versionRow.style.cssText = fwRowStyle;
    var versionLabel = el("span");
    versionLabel.style.cssText = "font-size:.9rem";
    versionLabel.innerHTML = '<span style="color:var(--text2)">Installed</span> ' +
      esc(S.firmware || S.installed_version || "unknown");
    var checkBtn = el("button", "btn btn-secondary btn-sm");
    checkBtn.textContent = "Check for Update";
    var statusMsg = el("span");
    statusMsg.style.cssText = "font-size:.8rem;color:var(--text2)";
    versionRow.appendChild(versionLabel);
    var checkWrap = el("div");
    checkWrap.style.cssText = "display:flex;align-items:center;gap:8px;flex-shrink:0";
    checkWrap.appendChild(statusMsg);
    checkWrap.appendChild(checkBtn);
    versionRow.appendChild(checkWrap);
    fwBody.appendChild(versionRow);

    var updateRow = el("div");
    updateRow.style.marginBottom = "8px";
    fwBody.appendChild(updateRow);
    var betaRow = el("div");
    betaRow.style.marginBottom = "12px";
    fwBody.appendChild(betaRow);

    function renderUpdateRow() {
      updateRow.innerHTML = "";
      if (!S.update_available) return;
      var row = el("div", "field");
      row.style.cssText = fwRowStyle;
      var label = el("span");
      label.style.cssText = "font-size:.9rem";
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
      if (!S.beta_opt_in || !S.beta_available) return;
      var row = el("div", "field");
      row.style.cssText = fwRowStyle;
      var label = el("span");
      label.style.cssText = "font-size:.9rem";
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
          if (S.beta_opt_in) {
            return safeGet(endpoints.update_beta);
          }
          return null;
        })
        .then(function (betaData) {
          if (betaData && (betaData.latest_version || betaData.value)) {
            S.beta_version = betaData.latest_version || betaData.value;
            S.beta_available = betaData.current_version
              ? betaData.latest_version !== betaData.current_version
              : betaData.state === "UPDATE AVAILABLE";
          }
          renderBetaRow();
          if (!S.update_available && !(S.beta_opt_in && S.beta_available)) {
            statusMsg.textContent = "Up to date";
            statusMsg.style.color = "var(--success)";
          }
        });
    };

    var fBetaUpd = field("");
    var betaTr = el("div", "toggle-row");
    betaTr.innerHTML = "<span>Pre-release Updates</span>";
    var betaTog = el("div", S.beta_opt_in ? "toggle on" : "toggle");
    betaTog.onclick = function () {
      S.beta_opt_in = !S.beta_opt_in;
      betaTog.className = S.beta_opt_in ? "toggle on" : "toggle";
      localStorage.setItem("beta_opt_in", S.beta_opt_in ? "true" : "false");
      if (S.beta_opt_in) {
        safeGet(endpoints.update_beta).then(function (betaData) {
          if (betaData && (betaData.latest_version || betaData.value)) {
            S.beta_version = betaData.latest_version || betaData.value;
            S.beta_available = betaData.current_version
              ? betaData.latest_version !== betaData.current_version
              : betaData.state === "UPDATE AVAILABLE";
          }
          renderBetaRow();
        });
      } else {
        betaRow.innerHTML = "";
      }
    };
    betaTr.appendChild(betaTog);
    fBetaUpd.appendChild(betaTr);
    fwBody.appendChild(fBetaUpd);

    var fAutoUpd = field("");
    var autoTr = el("div", "toggle-row");
    autoTr.innerHTML = "<span>Auto Update</span>";
    var autoTog = el("div", S.auto_update ? "toggle on" : "toggle");
    autoTog.onclick = function () {
      S.auto_update = !S.auto_update;
      autoTog.className = S.auto_update ? "toggle on" : "toggle";
      post(
        endpoints.auto_update + (S.auto_update ? "/turn_on" : "/turn_off")
      );
      freqField.style.display = S.auto_update ? "" : "none";
    };
    autoTr.appendChild(autoTog);
    fAutoUpd.appendChild(autoTr);
    fwBody.appendChild(fAutoUpd);

    var freqField = field("Update Frequency");
    freqField.appendChild(
      selectFromOptions(S.update_freq_options, S.update_frequency, function (v) {
        S.update_frequency = v;
        post(endpoints.update_frequency + "/set", { option: v });
      })
    );
    freqField.style.display = S.auto_update ? "" : "none";
    fwBody.appendChild(freqField);

    wrap.appendChild(makeCollapsibleCard("Firmware", fwBody, true));

    // Logs
    var logsBody = el("div");
    var logPre = document.createElement("pre");
    logPre.className = "log-output";
    logPreRef = logPre;
    var parts = [];
    for (var i = 0; i < logLines.length; i++) {
      var ln = logLines[i];
      var cls = logLevelClass(ln);
      parts.push(cls ? '<span class="' + cls + '">' + esc(ln) + '</span>' : '<span>' + esc(ln) + '</span>');
    }
    logPre.innerHTML = parts.join("\n");
    logPre.scrollTop = logPre.scrollHeight;

    logsBody.appendChild(logPre);
    var logsCard = makeCollapsibleCard("Device Logs", logsBody, true);
    logsCard.classList.add("card-logs");
    wrap.appendChild(logsCard);

    app.appendChild(wrap);

    if (S.firmware) {
      var verLine = el("div", "version");
      verLine.textContent = "v" + S.firmware;
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

  function makeCollapsibleCard(title, bodyElement, defaultCollapsed) {
    var card = el("div", "card");
    var header = el("div", "card-header");
    var h3 = document.createElement("h3");
    h3.textContent = title;
    var chevron = el("span", "card-chevron");
    chevron.innerHTML = "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M6 9l6 6 6-6\"/></svg>";
    header.appendChild(h3);
    header.appendChild(chevron);
    var body = el("div", "card-body");
    body.appendChild(bodyElement);
    card.appendChild(header);
    card.appendChild(body);
    if (defaultCollapsed) card.classList.add("collapsed");
    header.onclick = function () { card.classList.toggle("collapsed"); };
    return card;
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

  function input(type, value, placeholder) {
    var i = document.createElement("input");
    i.type = type;
    i.value = value || "";
    if (placeholder) i.placeholder = placeholder;
    return i;
  }

  function esc(s) {
    var d = document.createElement("div");
    d.textContent = s;
    return d.innerHTML;
  }

  // --- Init ---

  app.innerHTML =
    '<div style="text-align:center;padding:60px 0;color:#999">Loading\u2026</div>';
  initSSE();
})();
