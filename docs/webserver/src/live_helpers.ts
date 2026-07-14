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
        saveSetting(key, v);
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

  function setBadgeActive(badge, isActive) {
    if (!badge) return;
    badge.className = "on-badge" + (isActive ? " active" : "");
  }

  function setStatus(target, msg, type, clearAfterMs) {
    if (!target) return;
    target.replaceChildren();
    if (!msg) {
      target.textContent = "";
      return;
    }
    var dot = el("span", "dot " + (type || "green"));
    target.appendChild(dot);
    target.appendChild(document.createTextNode(" " + msg));
    clearTimeout(target._t);
    if (clearAfterMs) {
      target._t = setTimeout(function () {
        target.textContent = "";
      }, clearAfterMs);
    }
  }

  function button(text, cls, onClick) {
    var btn = el("button", cls || "btn btn-secondary");
    btn.type = "button";
    btn.textContent = text;
    if (onClick) btn.onclick = onClick;
    return btn;
  }

  function actionRow(labelEl, actionEl) {
    var row = el("div", "field fw-row");
    row.appendChild(labelEl);
    row.appendChild(actionEl);
    return row;
  }

  function textLabel(prefix, value) {
    var label = el("span", "fw-label");
    if (prefix) {
      var prefixEl = el("span");
      prefixEl.style.color = "var(--text2)";
      prefixEl.textContent = prefix;
      label.appendChild(prefixEl);
      label.appendChild(document.createTextNode(" " + value));
    } else {
      label.textContent = value;
    }
    return label;
  }

  function toggleSettingRow(options) {
    var opts = options || {};
    var f = field("");
    var row = el("div", "toggle-row");
    var label = el("span");
    label.textContent = opts.label || "";
    var getValue = opts.getValue || function () { return !!opts.value; };
    var setValue = opts.setValue || function (value) { opts.value = value; };
    var toggle = el("div", opts.value ? "toggle on" : "toggle");
    if (opts.disabled) {
      toggle.style.opacity = ".35";
      toggle.style.cursor = "not-allowed";
      if (opts.disabledTitle) toggle.title = opts.disabledTitle;
    }
    toggle.onclick = function () {
      if (opts.disabled) return;
      var next = !getValue();
      setValue(next);
      toggle.className = next ? "toggle on" : "toggle";
      if (opts.details) opts.details.style.display = next ? "" : "none";
      if (opts.badge) setBadgeActive(opts.badge, opts.badgeActive ? opts.badgeActive() : next);
      if (opts.onChange) opts.onChange(next);
    };
    row.appendChild(label);
    row.appendChild(toggle);
    f.appendChild(row);
    return { field: f, toggle: toggle };
  }

  function rangeSettingField(labelText, key, options) {
    var opts = options || {};
    var f = field(labelText || "");
    var rw = el("div", "range-wrap");
    if (opts.leftLabel) {
      var left = el("span", "range-label");
      left.textContent = opts.leftLabel;
      rw.appendChild(left);
    }
    var slider = document.createElement("input");
    slider.type = "range";
    slider.min = productNumberMin(key, opts.minFallback);
    slider.max = productNumberMax(key, opts.maxFallback);
    slider.step = productNumberStep(key, opts.stepFallback);
    slider.value = S[key];
    rw.appendChild(slider);
    if (opts.rightLabel) {
      var right = el("span", "range-label");
      right.textContent = opts.rightLabel;
      rw.appendChild(right);
    }
    if (opts.valueSuffix != null) {
      var value = el("span", "range-val");
      value.textContent = Math.round(S[key]) + opts.valueSuffix;
      slider.oninput = function () {
        value.textContent = slider.value + opts.valueSuffix;
      };
      rw.appendChild(value);
    }
    slider.onchange = function () {
      saveSetting(key, slider.value);
      if (opts.onChange) opts.onChange(slider.value);
    };
    f.appendChild(rw);
    return { field: f, input: slider };
  }

  function hourSelectSettingField(labelText, key) {
    var min = productNumberMin(key, 0);
    var max = productNumberMax(key, 23);
    var options = [];
    for (var h = min; h <= max; h++) options.push(h);
    var f = field(labelText);
    f.appendChild(
      selectFromOptions(options, Math.round(S[key]), function (v) {
        saveSetting(key, parseInt(v));
      }, formatHour)
    );
    return f;
  }

  function makeFieldError() {
    return el("div", "field-error");
  }

  function photoIdListField(options) {
    var opts = options || {};
    var f = field(opts.label);
    var list = el("div", "photo-id-list");
    var idInputs = [];
    var labelInputs = [];
    var error = makeFieldError();
    var moveUpIcon = "<svg viewBox=\"0 0 24 24\" width=\"18\" height=\"18\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M12 19V5\"/><path d=\"M5 12l7-7 7 7\"/></svg>";
    var moveDownIcon = "<svg viewBox=\"0 0 24 24\" width=\"18\" height=\"18\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M12 5v14\"/><path d=\"M19 12l-7 7-7-7\"/></svg>";
    var removeIcon = "<svg viewBox=\"0 0 24 24\" width=\"18\" height=\"18\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M3 6h18\"/><path d=\"M8 6V4h8v2\"/><path d=\"M19 6l-1 14H6L5 6\"/><path d=\"M10 11v5\"/><path d=\"M14 11v5\"/></svg>";

    function notify(changes, delayMs) {
      if (opts.onChange) opts.onChange(changes, delayMs);
    }

    function refreshRowButtons() {
      var rows = Array.prototype.slice.call(list.querySelectorAll(".photo-id-row"));
      rows.forEach(function (row, index) {
        var removeBtn = row.querySelector(".photo-id-remove");
        var moveUpBtn = row.querySelector(".photo-id-move-up");
        var moveDownBtn = row.querySelector(".photo-id-move-down");
        if (removeBtn) removeBtn.disabled = idInputs.length <= 1;
        if (moveUpBtn) moveUpBtn.disabled = index === 0;
        if (moveDownBtn) moveDownBtn.disabled = index === rows.length - 1;
      });
    }

    function movePhotoIdRow(row, direction) {
      var rows = Array.prototype.slice.call(list.querySelectorAll(".photo-id-row"));
      var fromIndex = rows.indexOf(row);
      var toIndex = fromIndex + direction;
      if (fromIndex < 0 || toIndex < 0 || toIndex >= rows.length) return;

      var movedId = idInputs.splice(fromIndex, 1)[0];
      var movedLabel = labelInputs.splice(fromIndex, 1)[0];
      idInputs.splice(toIndex, 0, movedId);
      labelInputs.splice(toIndex, 0, movedLabel);

      if (direction < 0) {
        list.insertBefore(row, rows[toIndex]);
      } else {
        list.insertBefore(rows[toIndex], row);
      }
      refreshRowButtons();
      notify(opts.reorderChanges, 0);
    }

    function addRow(value, labelValue) {
      var row = el("div", "photo-id-row");
      var fields = el("div", "photo-id-fields");
      var idInput = input("text", value || "", opts.idPlaceholder, MAX_PHOTO_ID_FIELD_LENGTH);
      var labelInput = input("text", labelValue || "", opts.labelPlaceholder, MAX_PHOTO_ID_FIELD_LENGTH);
      var actions = el("div", "photo-id-row-actions");
      var moveUpTitle = opts.moveUpTitle || "Move up";
      var moveDownTitle = opts.moveDownTitle || "Move down";
      var moveUpBtn = el("button", "btn btn-secondary btn-icon photo-id-move-up");
      moveUpBtn.type = "button";
      moveUpBtn.innerHTML = moveUpIcon;
      moveUpBtn.title = moveUpTitle;
      moveUpBtn.setAttribute("aria-label", moveUpTitle);
      moveUpBtn.onclick = function () {
        movePhotoIdRow(row, -1);
      };
      var moveDownBtn = el("button", "btn btn-secondary btn-icon photo-id-move-down");
      moveDownBtn.type = "button";
      moveDownBtn.innerHTML = moveDownIcon;
      moveDownBtn.title = moveDownTitle;
      moveDownBtn.setAttribute("aria-label", moveDownTitle);
      moveDownBtn.onclick = function () {
        movePhotoIdRow(row, 1);
      };
      var removeBtn = el("button", "btn btn-secondary btn-icon");
      removeBtn.classList.add("photo-id-remove");
      removeBtn.type = "button";
      removeBtn.innerHTML = removeIcon;
      removeBtn.title = opts.removeTitle;
      removeBtn.setAttribute("aria-label", opts.removeTitle);
      removeBtn.onclick = function () {
        if (idInputs.length <= 1) {
          idInput.value = "";
          labelInput.value = "";
          notify(opts.clearChanges, 0);
          return;
        }
        var removeIndex = idInputs.indexOf(idInput);
        idInputs.splice(removeIndex, 1);
        labelInputs.splice(removeIndex, 1);
        row.parentNode.removeChild(row);
        refreshRowButtons();
        notify(opts.clearChanges, 0);
      };
      idInput.oninput = function () {
        notify(opts.idChanges);
      };
      labelInput.oninput = function () {
        notify(opts.labelChanges);
      };
      fields.appendChild(idInput);
      fields.appendChild(labelInput);
      row.appendChild(fields);
      actions.appendChild(moveUpBtn);
      actions.appendChild(moveDownBtn);
      actions.appendChild(removeBtn);
      row.appendChild(actions);
      list.appendChild(row);
      idInputs.push(idInput);
      labelInputs.push(labelInput);
      refreshRowButtons();
    }

    var ids = splitPhotoIdList(S[opts.idKey]);
    var labels = parsePhotoLabelList(S[opts.labelKey]);
    for (var i = 0; i < Math.max(ids.length, labels.length, 1); i++) {
      addRow(ids[i] || "", labels[i] || "");
    }

    var addRowWrap = el("div", "photo-id-actions");
    var addBtn = button(opts.addText, "btn btn-secondary", function () {
      addRow("", "");
      idInputs[idInputs.length - 1].focus();
    });
    addBtn.title = opts.addText;
    addBtn.setAttribute("aria-label", opts.addText);
    addRowWrap.appendChild(addBtn);
    f.appendChild(list);
    f.appendChild(addRowWrap);
    f.appendChild(error);

    return {
      field: f,
      error: error,
      getIdsValue: function () {
        return idInputs.map(function (inputEl) {
          return inputEl.value.trim();
        }).filter(Boolean).join(",");
      },
      getLabelsValue: function () {
        return buildPhotoLabelList(idInputs, labelInputs);
      }
    };
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
    exportBtn.textContent = "Export";
    exportBtn.onclick = exportConfig;
    var importBtn = el("button", "btn btn-secondary");
    importBtn.textContent = "Import";
    importBtn.onclick = importConfig;
    backupRow.appendChild(exportBtn);
    backupRow.appendChild(importBtn);
    backupBody.appendChild(backupRow);
    return makeCollapsibleCard("Backup", backupBody, true);
  }

  function makeImportSettingsCard() {
    var importBody = el("div");
    var importBtn = el("button", "btn btn-secondary btn-block");
    importBtn.textContent = "Import Settings";
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
        saveSetting(spec.key, serverInput.value);
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
