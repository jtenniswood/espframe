  // --- SSE live updates (after render) ---

  function handleLiveEvent(d) {
    if (!d || !d.id) return;
    var id = d.id;
    var stateSpec = ENTITY_STATE_MAP[id];
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
    } else if (stateSpec && LIVE_RENDER_STATE_KEYS.indexOf(stateSpec.key) !== -1) {
      applyEntityToState(d);
      if (!isEditingSetting()) renderSettings();
    } else if (stateSpec && liveRenderStateKeyHasPrefix(stateSpec.key)) {
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
    var fallback = PRODUCT_SETTINGS && PRODUCT_SETTINGS.schedule_wake_timeout &&
      PRODUCT_SETTINGS.schedule_wake_timeout.default !== undefined
      ? PRODUCT_SETTINGS.schedule_wake_timeout.default
      : 60;
    var min = productNumberMin("schedule_wake_timeout", 10);
    var max = productNumberMax("schedule_wake_timeout", 3600);
    if (!seconds) seconds = fallback;
    if (seconds < min) seconds = min;
    if (seconds > max) seconds = max;
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

  function productSelectSettingField(labelText, key, options) {
    var opts = options || {};
    var f = field(labelText);
    var current = opts.current !== undefined ? opts.current : S[key];
    f.appendChild(
      selectFromOptions(productSettingOptions(key, opts.includeDeveloper), current, function (v) {
        S[key] = v;
        post(endpoints[key] + "/set", { option: v });
        if (opts.onChange) opts.onChange(v);
      }, opts.optionDisplayFn)
    );
    return f;
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
    current = normalizeTimezoneOption(current);
    return selectFromOptions(options, current, function (v) {
      onChange(normalizeTimezoneOption(v));
    }, function (o) {
      return timezoneDisplayLabel(o);
    });
  }

  function normalizeTimezoneOption(value) {
    if (value === "Asia/Almaty (GMT+6)") return "Asia/Almaty (GMT+5)";
    return value;
  }

  function timezoneDisplayLabel(option) {
    var label = (S.tz_labels && S.tz_labels[option]) || option;
    return label.replace(/_/g, " ");
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

  function ntpServersField() {
    var f = field("NTP Servers");
    var list = el("div", "photo-id-list");
    [
      { key: "ntp_server_1", placeholder: "0.pool.ntp.org", label: "NTP Server 1" },
      { key: "ntp_server_2", placeholder: "1.pool.ntp.org", label: "NTP Server 2" },
      { key: "ntp_server_3", placeholder: "2.pool.ntp.org", label: "NTP Server 3" }
    ].forEach(function (spec) {
      var serverInput = input("text", S[spec.key], spec.placeholder, MAX_NTP_SERVER_LENGTH);
      serverInput.setAttribute("aria-label", spec.label);
      serverInput.onchange = function () {
        saveNtpServer(spec.key, serverInput.value);
        serverInput.value = S[spec.key];
      };
      list.appendChild(serverInput);
    });
    f.appendChild(list);
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
