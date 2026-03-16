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
    interval_min: 5,
    interval_max: 300,
    interval_step: 5,
    interval: 30,
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
    day_night_enabled: true,
    brightness_day: 100,
    brightness_night: 75,
    sunrise: "",
    sunset: "",
    photo_source: "All Photos",
    photo_source_options: ["All Photos", "Favorites", "Album", "Person"],
    album_ids: "",
    person_ids: "",
  };

  var app = document.getElementById("app");
  if (!app) {
    app = document.createElement("div");
    app.id = "app";
    document.body.appendChild(app);
  }

  var endpoints = {
    immich_url: "/text/immich_url",
    api_key: "/text/immich_api_key",
    clock_format: "/select/clock_format",
    timezone: "/select/timezone",
    interval: "/number/slideshow_interval",
    backlight: "/light/backlight",
    show_clock: "/switch/show_clock",
    firmware: "/text_sensor/firmware_version",
    update: "/update/firmware_update",
    update_beta: "/update/firmware_update_beta",
    auto_update: "/switch/auto_update",
    update_frequency: "/select/update_frequency",
    schedule_enabled: "/switch/screen_schedule",
    schedule_on_hour: "/number/schedule_on_hour",
    schedule_off_hour: "/number/schedule_off_hour",
    day_night_enabled: "/switch/automatic_brightness",
    brightness_day: "/number/daytime_brightness",
    brightness_night: "/number/nighttime_brightness",
    sunrise: "/text_sensor/sunrise_time",
    sunset: "/text_sensor/sunset_time",
    photo_source: "/select/photo_source",
    album_ids: "/text/album_ids",
    person_ids: "/text/person_ids",
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

  function collectState(d) {
    if (!d || !d.id) return;
    var id = d.id;
    if (id === "text-immich_url") {
      S.immich_url = d.value || "";
    } else if (id === "text-immich_api_key") {
      S.api_key = d.value || "";
    } else if (id === "select-clock_format") {
      S.clock_format = d.value || "24 Hour";
      if (d.option && d.option.length) S.clock_options = d.option;
    } else if (id === "select-timezone") {
      S.timezone = d.value || "";
      if (d.option && d.option.length) S.tz_options = d.option;
    } else if (id === "number-slideshow_interval") {
      S.interval = d.value != null ? d.value : 30;
      if (d.min_value != null) S.interval_min = d.min_value;
      if (d.max_value != null) S.interval_max = d.max_value;
      if (d.step != null) S.interval_step = d.step;
    } else if (id === "light-backlight") {
      S.backlight_on = d.state === "ON";
      if (d.brightness != null)
        S.brightness = Math.round((d.brightness / 255) * 100);
    } else if (id === "switch-show_clock") {
      S.show_clock = d.value === true || d.state === "ON";
    } else if (id === "text_sensor-firmware_version") {
      S.firmware = d.value || d.state || "";
    } else if (id === "update-firmware_update") {
      S.installed_version = d.current_version || "";
      S.latest_version = d.latest_version || "";
      S.update_available =
        S.installed_version &&
        S.latest_version &&
        S.installed_version !== S.latest_version;
    } else if (id === "update-firmware_update_beta") {
      S.beta_version = d.latest_version || "";
      S.beta_available =
        S.beta_version &&
        d.current_version &&
        S.beta_version !== d.current_version;
    } else if (id === "switch-auto_update") {
      S.auto_update = d.value === true || d.state === "ON";
    } else if (id === "select-update_frequency") {
      S.update_frequency = d.value || "Daily";
      if (d.option && d.option.length) S.update_freq_options = d.option;
    } else if (id === "switch-screen_schedule") {
      S.schedule_enabled = d.value === true || d.state === "ON";
    } else if (id === "number-schedule_on_hour") {
      S.schedule_on_hour = d.value != null ? d.value : 6;
    } else if (id === "number-schedule_off_hour") {
      S.schedule_off_hour = d.value != null ? d.value : 23;
    } else if (id === "switch-automatic_brightness") {
      S.day_night_enabled = d.value === true || d.state === "ON";
    } else if (id === "number-daytime_brightness") {
      S.brightness_day = d.value != null ? d.value : 100;
    } else if (id === "number-nighttime_brightness") {
      S.brightness_night = d.value != null ? d.value : 75;
    } else if (id === "text_sensor-sunrise_time") {
      S.sunrise = d.value || d.state || "";
    } else if (id === "text_sensor-sunset_time") {
      S.sunset = d.value || d.state || "";
    } else if (id === "select-photo_source") {
      S.photo_source = d.value || "All Photos";
      if (d.option && d.option.length) S.photo_source_options = d.option;
    } else if (id === "text-album_ids") {
      S.album_ids = d.value || "";
    } else if (id === "text-person_ids") {
      S.person_ids = d.value || "";
    }
  }

  function tryRender() {
    if (rendered) return;
    rendered = true;
    if (!S.immich_url) {
      renderWizard();
    } else {
      renderSettings();
    }
  }

  function initSSE() {
    try {
      evtSource = new EventSource("/events");

      evtSource.addEventListener("state", function (e) {
        try {
          var d = JSON.parse(e.data);
          collectState(d);
        } catch (_) {}

        if (!rendered) {
          clearTimeout(renderTimer);
          renderTimer = setTimeout(tryRender, 500);
        }
      });

      evtSource.onerror = function () {
        setStatus(false);
        if (!rendered) {
          clearTimeout(renderTimer);
          renderTimer = setTimeout(tryRender, 1000);
        }
      };

      evtSource.onopen = function () {
        setStatus(true);
      };
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
      var urlInput = input("url", S.immich_url, "https://immich.example.com");
      f1.appendChild(urlInput);
      card.appendChild(f1);

      var f2 = field("API Key");
      var grp = el("div", "input-group");
      var keyInput = input("password", S.api_key, "Your Immich API key");
      var showBtn = el("button", "btn btn-secondary btn-sm");
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
      var fmtSel = document.createElement("select");
      fmtSel.className = "select";
      S.clock_options.forEach(function (opt) {
        var o = document.createElement("option");
        o.value = opt;
        o.textContent = opt;
        if (opt === S.clock_format) o.selected = true;
        fmtSel.appendChild(o);
      });
      fmtSel.onchange = function () {
        S.clock_format = fmtSel.value;
        post(endpoints.clock_format + "/set", { option: fmtSel.value });
      };
      f1.appendChild(fmtSel);
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
    var conn = el("div", "card");
    conn.innerHTML = "<h3>Connection</h3>";

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
    var urlInput = input("url", S.immich_url, "https://immich.example.com");
    urlInput.onchange = function () {
      post(endpoints.immich_url + "/set", { value: urlInput.value.trim() });
      showSaved("URL saved");
    };
    f1.appendChild(urlInput);
    conn.appendChild(f1);

    var f2 = field("API Key");
    var keyConfigured = S.api_key && S.api_key.length > 0;
    var keyWrap = el("div");

    if (keyConfigured) {
      var keyStatus = el("div", "input-group");
      var maskedDisplay = el("div");
      maskedDisplay.style.cssText =
        "flex:1;padding:10px 12px;background:var(--surface2);border:1px solid var(--border);" +
        "border-radius:6px;color:var(--text2);font-size:.9rem;letter-spacing:2px";
      maskedDisplay.textContent = "\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022";
      var changeBtn = el("button", "btn btn-secondary btn-sm");
      changeBtn.textContent = "Change";
      changeBtn.type = "button";
      changeBtn.onclick = function () {
        keyWrap.innerHTML = "";
        keyWrap.appendChild(makeKeyInput());
      };
      keyStatus.appendChild(maskedDisplay);
      keyStatus.appendChild(changeBtn);
      keyWrap.appendChild(keyStatus);
    } else {
      keyWrap.appendChild(makeKeyInput());
    }

    function makeKeyInput() {
      var grp = el("div", "input-group");
      var keyInput = input("text", "", "Paste your Immich API key");
      keyInput.onchange = function () {
        var v = keyInput.value.trim();
        if (!v) return;
        post(endpoints.api_key + "/set", { value: v });
        showSaved("API key saved");
        keyInput.value = "";
        keyWrap.innerHTML = "";
        var saved = el("div", "input-group");
        var mask = el("div");
        mask.style.cssText =
          "flex:1;padding:10px 12px;background:var(--surface2);border:1px solid var(--border);" +
          "border-radius:6px;color:var(--text2);font-size:.9rem;letter-spacing:2px";
        mask.textContent = "\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022";
        var cb = el("button", "btn btn-secondary btn-sm");
        cb.textContent = "Change";
        cb.type = "button";
        cb.onclick = function () {
          keyWrap.innerHTML = "";
          keyWrap.appendChild(makeKeyInput());
        };
        saved.appendChild(mask);
        saved.appendChild(cb);
        keyWrap.appendChild(saved);
      };
      grp.appendChild(keyInput);
      return grp;
    }

    f2.appendChild(keyWrap);
    conn.appendChild(f2);

    conn.appendChild(connStatus);
    wrap.appendChild(conn);

    // Photo Source
    var src = el("div", "card");
    src.innerHTML = "<h3>Photo Source</h3>";

    var fSrc = field("Source");
    var srcSel = document.createElement("select");
    srcSel.className = "select";
    S.photo_source_options.forEach(function (opt) {
      var o = document.createElement("option");
      o.value = opt;
      o.textContent = opt;
      if (opt === S.photo_source) o.selected = true;
      srcSel.appendChild(o);
    });

    var albumField = field("Album IDs");
    var albumInput = input("text", S.album_ids, "Paste album IDs, comma-separated");
    albumInput.onchange = function () {
      post(endpoints.album_ids + "/set", { value: albumInput.value.trim() });
    };
    var albumHint = el("div");
    albumHint.style.cssText = "font-size:.75rem;color:var(--text2);margin-top:4px";
    albumHint.textContent = "Find IDs in your Immich server URL bar";
    albumField.appendChild(albumInput);
    albumField.appendChild(albumHint);
    albumField.style.display = S.photo_source === "Album" ? "" : "none";

    var personField = field("Person IDs");
    var personInput = input("text", S.person_ids, "Paste person IDs, comma-separated");
    personInput.onchange = function () {
      post(endpoints.person_ids + "/set", { value: personInput.value.trim() });
    };
    var personHint = el("div");
    personHint.style.cssText = "font-size:.75rem;color:var(--text2);margin-top:4px";
    personHint.textContent = "Find IDs in your Immich server URL bar";
    personField.appendChild(personInput);
    personField.appendChild(personHint);
    personField.style.display = S.photo_source === "Person" ? "" : "none";

    srcSel.onchange = function () {
      S.photo_source = srcSel.value;
      post(endpoints.photo_source + "/set", { option: srcSel.value });
      albumField.style.display = srcSel.value === "Album" ? "" : "none";
      personField.style.display = srcSel.value === "Person" ? "" : "none";
    };

    fSrc.appendChild(srcSel);
    src.appendChild(fSrc);
    src.appendChild(albumField);
    src.appendChild(personField);
    wrap.appendChild(src);

    // Display
    var disp = el("div", "card");
    disp.innerHTML = "<h3>Display</h3>";

    var f3 = field("Slideshow Interval");
    var rw = el("div", "range-wrap");
    var slider = document.createElement("input");
    slider.type = "range";
    slider.min = S.interval_min;
    slider.max = S.interval_max;
    slider.step = S.interval_step;
    slider.value = S.interval;
    var valLabel = el("span", "range-val");
    valLabel.textContent = S.interval + "s";
    slider.oninput = function () {
      valLabel.textContent = slider.value + "s";
    };
    slider.onchange = function () {
      post(endpoints.interval + "/set", { value: slider.value });
    };
    rw.appendChild(slider);
    rw.appendChild(valLabel);
    f3.appendChild(rw);
    disp.appendChild(f3);

    var f4 = field("Backlight");
    var rw2 = el("div", "range-wrap");
    var bSlider = document.createElement("input");
    bSlider.type = "range";
    bSlider.min = 0;
    bSlider.max = 100;
    bSlider.step = 1;
    bSlider.value = S.backlight_on ? S.brightness : 0;
    var bVal = el("span", "range-val");
    bVal.textContent = (S.backlight_on ? S.brightness : 0) + "%";
    bSlider.oninput = function () {
      bVal.textContent = bSlider.value + "%";
    };
    bSlider.onchange = function () {
      var pct = parseInt(bSlider.value);
      if (pct === 0) {
        post(endpoints.backlight + "/turn_off");
      } else {
        post(endpoints.backlight + "/turn_on", {
          brightness: Math.round((pct / 100) * 255),
        });
      }
    };
    rw2.appendChild(bSlider);
    rw2.appendChild(bVal);
    f4.appendChild(rw2);
    disp.appendChild(f4);
    wrap.appendChild(disp);

    // Schedule
    var sched = el("div", "card");
    sched.innerHTML = "<h3>Screen Schedule</h3>";

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
    sched.appendChild(fSchedToggle);

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

    sched.appendChild(schedDetails);
    wrap.appendChild(sched);

    // Day/Night Brightness
    var dnCard = el("div", "card");
    dnCard.innerHTML = "<h3>Automatic Brightness</h3>";

    var fDnToggle = field("");
    var dnTr = el("div", "toggle-row");
    dnTr.innerHTML = "<span>Day/Night Brightness</span>";
    var dnTog = el("div", S.day_night_enabled ? "toggle on" : "toggle");
    var dnDetails = el("div");
    dnDetails.style.display = S.day_night_enabled ? "" : "none";

    dnTog.onclick = function () {
      S.day_night_enabled = !S.day_night_enabled;
      dnTog.className = S.day_night_enabled ? "toggle on" : "toggle";
      dnDetails.style.display = S.day_night_enabled ? "" : "none";
      post(endpoints.day_night_enabled + (S.day_night_enabled ? "/turn_on" : "/turn_off"));
    };
    dnTr.appendChild(dnTog);
    fDnToggle.appendChild(dnTr);
    dnCard.appendChild(fDnToggle);

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

    if (S.sunrise || S.sunset) {
      var fSunInfo = el("div", "field sun-info");
      var sunText = "";
      if (S.sunrise) sunText += "Sunrise: " + esc(S.sunrise);
      if (S.sunrise && S.sunset) sunText += " \u00a0/\u00a0 ";
      if (S.sunset) sunText += "Sunset: " + esc(S.sunset);
      fSunInfo.innerHTML = sunText;
      dnDetails.appendChild(fSunInfo);
    }

    dnCard.appendChild(dnDetails);
    wrap.appendChild(dnCard);

    // Clock
    var clk = el("div", "card");
    clk.innerHTML = "<h3>Clock</h3>";

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
    clk.appendChild(f5);

    var f6 = field("Format");
    var fmtSel = document.createElement("select");
    fmtSel.className = "select";
    S.clock_options.forEach(function (opt) {
      var o = document.createElement("option");
      o.value = opt;
      o.textContent = opt;
      if (opt === S.clock_format) o.selected = true;
      fmtSel.appendChild(o);
    });
    fmtSel.onchange = function () {
      S.clock_format = fmtSel.value;
      post(endpoints.clock_format + "/set", { option: fmtSel.value });
    };
    f6.appendChild(fmtSel);
    clk.appendChild(f6);

    var f7 = field("Timezone");
    f7.appendChild(
      timezoneSelect(S.tz_options, S.timezone, function (v) {
        post(endpoints.timezone + "/set", { option: v });
        S.timezone = v;
      })
    );
    clk.appendChild(f7);
    wrap.appendChild(clk);

    // Firmware
    var fw = el("div", "card");
    fw.innerHTML = "<h3>Firmware</h3>";

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
    fw.appendChild(versionRow);

    var updateRow = el("div");
    updateRow.style.marginBottom = "8px";
    fw.appendChild(updateRow);
    var betaRow = el("div");
    betaRow.style.marginBottom = "12px";
    fw.appendChild(betaRow);

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
        post("/update/firmware_update/install");
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
      post("/button/check_for_update/press")
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
    fw.appendChild(fBetaUpd);

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
    fw.appendChild(fAutoUpd);

    var freqField = field("Update Frequency");
    var freqSel = document.createElement("select");
    freqSel.className = "select";
    S.update_freq_options.forEach(function (opt) {
      var o = document.createElement("option");
      o.value = opt;
      o.textContent = opt;
      if (opt === S.update_frequency) o.selected = true;
      freqSel.appendChild(o);
    });
    freqSel.onchange = function () {
      S.update_frequency = freqSel.value;
      post(endpoints.update_frequency + "/set", { option: freqSel.value });
    };
    freqField.appendChild(freqSel);
    freqField.style.display = S.auto_update ? "" : "none";
    fw.appendChild(freqField);

    wrap.appendChild(fw);

    // Logs
    var logs = el("div", "card card-logs");
    logs.innerHTML = "<h3>Device Logs</h3>";
    var logToggle = el("button", "btn btn-secondary btn-sm");
    logToggle.textContent = "Show Logs";
    var logWrap = el("div");
    logWrap.style.display = "none";
    var logPre = document.createElement("pre");
    logPre.className = "log-output";
    var logLines = [];
    var maxLines = 200;

    logToggle.onclick = function () {
      var visible = logWrap.style.display !== "none";
      logWrap.style.display = visible ? "none" : "block";
      logToggle.textContent = visible ? "Show Logs" : "Hide Logs";
      if (!visible) logPre.scrollTop = logPre.scrollHeight;
    };

    logWrap.appendChild(logPre);
    logs.appendChild(logToggle);
    logs.appendChild(logWrap);
    wrap.appendChild(logs);

    if (evtSource) {
      evtSource.addEventListener("log", function (e) {
        var line = e.data;
        logLines.push(line);
        if (logLines.length > maxLines) logLines.shift();
        logPre.textContent = logLines.join("\n");
        if (logWrap.style.display !== "none") {
          logPre.scrollTop = logPre.scrollHeight;
        }
      });
    }

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
    if (id === "light-backlight") {
      S.backlight_on = d.state === "ON";
      if (d.brightness != null)
        S.brightness = Math.round((d.brightness / 255) * 100);
    } else if (id === "switch-show_clock") {
      S.show_clock = d.state === "ON" || d.value === true;
    }
  }

  function setStatus() {}

  // --- Hour formatting ---

  function formatHour(h) {
    h = Math.round(h);
    if (h === 0) return "12:00 AM";
    if (h < 12) return h + ":00 AM";
    if (h === 12) return "12:00 PM";
    return (h - 12) + ":00 PM";
  }

  // --- Timezone Select ---

  function timezoneSelect(options, current, onChange) {
    var sel = document.createElement("select");
    sel.className = "select";
    options.forEach(function (o) {
      var opt = document.createElement("option");
      opt.value = o;
      opt.textContent = o.replace(/_/g, " ");
      if (o === current) opt.selected = true;
      sel.appendChild(opt);
    });
    sel.onchange = function () {
      onChange(sel.value);
    };
    return sel;
  }

  // --- Helpers ---

  function el(tag, cls) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    return e;
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
