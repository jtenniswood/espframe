  function makeScreenBrightnessCard() {
    // Screen Brightness
    var dnDetails = el("div");

    var fDayBrt = field("Daytime Brightness");
    var rwDay = el("div", "range-wrap");
    var daySlider = document.createElement("input");
    daySlider.type = "range";
    daySlider.min = productNumberMin("brightness_day", 10);
    daySlider.max = productNumberMax("brightness_day", 100);
    daySlider.step = productNumberStep("brightness_day", 5);
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
    nightSlider.min = productNumberMin("brightness_night", 10);
    nightSlider.max = productNumberMax("brightness_night", 100);
    nightSlider.step = productNumberStep("brightness_night", 5);
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

    return makeCollapsibleCard("Screen Brightness", dnDetails, true);

  }

  function makeScreenToneCard() {
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
    baseSlider.min = productNumberMin("base_tone", 0);
    baseSlider.max = productNumberMax("base_tone", 100);
    baseSlider.step = productNumberStep("base_tone", 5);
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
    warmSlider.min = productNumberMin("warm_tone_intensity", 10);
    warmSlider.max = productNumberMax("warm_tone_intensity", 100);
    warmSlider.step = productNumberStep("warm_tone_intensity", 5);
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
    return makeCollapsibleCard("Screen Tone", warmBody, true, toneBadge);

  }

  function makeNightScheduleCard() {
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
    var scheduleOnMin = productNumberMin("schedule_on_hour", 0);
    var scheduleOnMax = productNumberMax("schedule_on_hour", 23);
    for (var h = scheduleOnMin; h <= scheduleOnMax; h++) {
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
    var scheduleOffMin = productNumberMin("schedule_off_hour", 0);
    var scheduleOffMax = productNumberMax("schedule_off_hour", 23);
    for (var h2 = scheduleOffMin; h2 <= scheduleOffMax; h2++) {
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
    var scheduleWakeMin = productNumberMin("schedule_wake_timeout", 10);
    var scheduleWakeMax = productNumberMax("schedule_wake_timeout", 3600);
    var scheduleWakeOptions = [10, 30, 60, 120, 300, 600, 1800, 3600].filter(function (v) {
      return v >= scheduleWakeMin && v <= scheduleWakeMax;
    });
    var scheduleWakeCurrent = normalizeScheduleWakeTimeout(S.schedule_wake_timeout);
    if (scheduleWakeOptions.indexOf(scheduleWakeCurrent) === -1) {
      scheduleWakeOptions.push(scheduleWakeCurrent);
      scheduleWakeOptions.sort(function (a, b) { return a - b; });
    }
    fWakeTimeout.appendChild(
      selectFromOptions(scheduleWakeOptions, scheduleWakeCurrent, function (v) {
        S.schedule_wake_timeout = normalizeScheduleWakeTimeout(v);
        postScheduleWakeTimeout(S.schedule_wake_timeout);
      }, formatDurationSeconds)
    );
    schedDetails.appendChild(fWakeTimeout);

    schedBody.appendChild(schedDetails);
    return makeCollapsibleCard("Night Schedule", schedBody, true, schedBadge);

  }

  function makeRotationCard() {
    // Rotation
    var rotationBody = el("div");
    var fRotation = field("Rotation");
    var rotationOptions = screenRotationOptionsForUi();
    fRotation.appendChild(
      selectFromOptions(rotationOptions, effectiveScreenRotationForUi(), function (v) {
        S.screen_rotation = v;
        post(endpoints.screen_rotation + "/set", { option: v });
        S.portrait_pairing = !isPortraitScreenRotation(v);
        post(endpoints.portrait_pairing + (S.portrait_pairing ? "/turn_on" : "/turn_off"));
        renderSettings();
      }, function (v) {
        return v + " degrees";
      })
    );
    rotationBody.appendChild(fRotation);
    return makeCollapsibleCard("Rotation", rotationBody, true);

  }

  function makeClockCard() {
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
      selectFromOptions(productSettingOptions("clock_format"), S.clock_format, function (v) {
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
    clkBody.appendChild(ntpServersField());
    return makeCollapsibleCard("Clock", clkBody, true, clockBadge);

  }
