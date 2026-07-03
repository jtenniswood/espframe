  function makeScreenBrightnessCard() {
    // Screen Brightness
    var dnDetails = el("div");

    dnDetails.appendChild(rangeSettingField("Daytime Brightness", "brightness_day", {
      minFallback: 10,
      maxFallback: 100,
      stepFallback: 5,
      valueSuffix: "%"
    }).field);

    dnDetails.appendChild(rangeSettingField("Nighttime Brightness", "brightness_night", {
      minFallback: 10,
      maxFallback: 100,
      stepFallback: 5,
      valueSuffix: "%"
    }).field);

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

    var baseDetails = el("div");
    baseDetails.style.display = S.base_tone_enabled ? "" : "none";
    var fBaseToneToggle = toggleSettingRow({
      label: "Screen Tone Adjustment",
      value: S.base_tone_enabled,
      getValue: function () { return S.base_tone_enabled; },
      setValue: function (value) { S.base_tone_enabled = value; },
      details: baseDetails,
      badge: toneBadge,
      badgeActive: function () { return S.base_tone_enabled || S.warm_tones_enabled; },
      onChange: function () { saveSetting("base_tone_enabled", S.base_tone_enabled); }
    }).field;
    fBaseToneToggle.style.marginBottom = "8px";
    warmBody.appendChild(fBaseToneToggle);

    baseDetails.appendChild(rangeSettingField("", "base_tone", {
      minFallback: 0,
      maxFallback: 100,
      stepFallback: 5,
      leftLabel: "Cooler",
      rightLabel: "Warmer"
    }).field);
    baseDetails.style.marginBottom = "28px";
    warmBody.appendChild(baseDetails);

    var nightDetails = el("div");
    nightDetails.style.display = S.warm_tones_enabled ? "" : "none";
    var fWarmToggle = toggleSettingRow({
      label: "Night Tone Adjustment",
      value: S.warm_tones_enabled,
      getValue: function () { return S.warm_tones_enabled; },
      setValue: function (value) { S.warm_tones_enabled = value; },
      details: nightDetails,
      badge: toneBadge,
      badgeActive: function () { return S.base_tone_enabled || S.warm_tones_enabled; },
      onChange: function () { saveSetting("warm_tones_enabled", S.warm_tones_enabled); }
    }).field;
    fWarmToggle.style.marginBottom = "8px";
    warmBody.appendChild(fWarmToggle);

    nightDetails.appendChild(rangeSettingField("", "warm_tone_intensity", {
      minFallback: 10,
      maxFallback: 100,
      stepFallback: 5,
      leftLabel: "Cooler",
      rightLabel: "Warmer"
    }).field);

    nightDetails.appendChild(toggleSettingRow({
      label: "Turn on until sunrise",
      value: S.warm_tone_override,
      getValue: function () { return S.warm_tone_override; },
      setValue: function (value) { S.warm_tone_override = value; },
      onChange: function () { saveSetting("warm_tone_override", S.warm_tone_override); }
    }).field);

    warmBody.appendChild(nightDetails);
    return makeCollapsibleCard("Screen Tone", warmBody, true, toneBadge);

  }

  function makeNightScheduleCard() {
    // Schedule
    var schedBadge = makeBadge(S.schedule_enabled);
    var schedBody = el("div");
    var schedDetails = el("div");
    schedDetails.style.display = S.schedule_enabled ? "" : "none";
    schedBody.appendChild(toggleSettingRow({
      label: "Schedule Screen Off",
      value: S.schedule_enabled,
      getValue: function () { return S.schedule_enabled; },
      setValue: function (value) { S.schedule_enabled = value; },
      details: schedDetails,
      badge: schedBadge,
      onChange: function () { saveSetting("schedule_enabled", S.schedule_enabled); }
    }).field);

    schedDetails.appendChild(hourSelectSettingField("On Time", "schedule_on_hour"));
    schedDetails.appendChild(hourSelectSettingField("Off Time", "schedule_off_hour"));

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
        saveSetting("schedule_wake_timeout", v);
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
        saveSetting("screen_rotation", v);
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
    clkBody.appendChild(toggleSettingRow({
      label: "Show Clock",
      value: S.show_clock,
      getValue: function () { return S.show_clock; },
      setValue: function (value) { S.show_clock = value; },
      badge: clockBadge,
      onChange: function () { saveSetting("show_clock", S.show_clock); }
    }).field);

    var f6 = field("Format");
    f6.appendChild(
      selectFromOptions(productSettingOptions("clock_format"), S.clock_format, function (v) {
        saveSetting("clock_format", v);
      })
    );
    clkBody.appendChild(f6);

    var f7 = field("Timezone");
    f7.appendChild(
      timezoneSelect(S.tz_options, S.timezone, function (v) {
        saveSetting("timezone", v);
      })
    );
    clkBody.appendChild(f7);
    clkBody.appendChild(ntpServersField());
    return makeCollapsibleCard("Clock", clkBody, true, clockBadge);

  }
