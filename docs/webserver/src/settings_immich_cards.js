  function makeConnectionCard() {
    // Connection
    var connBody = el("div");
    var connStatus = el("div", "status mb-12");
    connStatus.id = "conn-status";

    function showSaved(msg) {
      setStatus(connStatus, msg || "Saved", "green", 3000);
    }

    function showConnectionError(msg) {
      setStatus(connStatus, msg, "red");
    }

    var urlField = makeConnectionUrlField(S.immich_url);
    var urlInput = urlField.input;
    urlInput.onchange = function () {
      var normalized = normalizeImmichUrl(urlInput.value);
      saveAndVerifyConnectionValue(
        endpoints.immich_url,
        normalized,
        true,
        function (saved) { return normalizeImmichUrl(saved) === normalized; }
      ).then(function () {
        S.immich_url = normalized;
        urlInput.value = normalized;
        showSaved("URL saved");
      }).catch(function () {
        showConnectionError("Failed to save URL");
      });
    };
    connBody.appendChild(urlField.field);

    var f2 = field("API Key");
    var keyConfigured = S.api_key && S.api_key.length > 0;
    var keyWrap = el("div");

    function showKeyMasked() {
      keyWrap.innerHTML = "";
      keyWrap.appendChild(makeMaskedApiKeyRow(function () {
        keyWrap.innerHTML = "";
        keyWrap.appendChild(makeKeyInput());
      }));
    }

    function makeKeyInput() {
      var keyControl = makeApiKeyInputGroup({
        type: "text",
        value: "",
        placeholder: "Paste your Immich API key",
        buttonText: "Save",
        buttonClass: "btn btn-primary",
        onButtonClick: function (keyInput, saveBtn) {
          var v = keyInput.value.trim();
          if (!v) return;
          saveBtn.disabled = true;
          saveBtn.textContent = "Saving\u2026";
          saveAndVerifyConnectionValue(
            endpoints.api_key,
            v,
            false,
            function (saved) { return !!saved; }
          ).then(function () {
            S.api_key = v;
            showSaved("API key saved");
            showKeyMasked();
          }).catch(function () {
            saveBtn.disabled = false;
            saveBtn.textContent = "Save";
            showConnectionError("Failed to save API key");
          });
        }
      });
      return keyControl.group;
    }

    if (keyConfigured) {
      showKeyMasked();
    } else {
      keyWrap.appendChild(makeKeyInput());
    }

    f2.appendChild(keyWrap);
    connBody.appendChild(f2);

    connBody.appendChild(productSelectSettingField("Connection Timeout", "conn_timeout"));

    connBody.appendChild(connStatus);
    return makeCollapsibleCard("Connection", connBody, true);

  }

  function makeFrequencyCard() {
    // Frequency
    var dispBody = el("div");
    dispBody.appendChild(productSelectSettingField("Slideshow Interval", "interval"));
    return makeCollapsibleCard("Frequency", dispBody, true);

  }

  function makePhotoSourceCard() {
    // Photo Source
    var srcBody = el("div");
    var photoSourceApplyTimer = null;
    var pendingPhotoSourceSave = {
      source: false,
      album: false,
      albumLabel: false,
      albumOrder: false,
      person: false,
      personLabel: false,
      tag: false,
      tagLabel: false
    };
    var fSrc = field("Source");
    var srcSel = selectFromOptions(productSettingOptions("photo_source"), S.photo_source, function (v) {
      S.photo_source = v;
      albumField.style.display = v === "Album" ? "" : "none";
      albumOrderField.style.display = v === "Album" ? "" : "none";
      personField.style.display = v === "Person" ? "" : "none";
      tagField.style.display = v === "Tag" ? "" : "none";
      schedulePhotoSourceApply(0, { source: true });
    });

    var albumOrderField = field("Album Order");
    albumOrderField.appendChild(
      selectFromOptions(productSettingOptions("album_order"), S.album_order, function (v) {
        S.album_order = v;
        schedulePhotoSourceApply(0, { albumOrder: true });
      })
    );
    albumOrderField.style.display = S.photo_source === "Album" ? "" : "none";

    var albumList = photoIdListField({
      label: "Albums",
      idKey: "album_ids",
      labelKey: "album_labels",
      idPlaceholder: "Paste album ID from Immich URL",
      labelPlaceholder: "What is it?",
      addText: "Add an album",
      removeTitle: "Remove album ID",
      reorderable: true,
      moveUpTitle: "Move album up",
      moveDownTitle: "Move album down",
      idChanges: { album: true, albumLabel: true },
      labelChanges: { albumLabel: true },
      clearChanges: { album: true, albumLabel: true },
      onChange: function (changes, delayMs) { schedulePhotoSourceApply(delayMs, changes); }
    });
    var albumField = albumList.field;
    albumField.style.display = S.photo_source === "Album" ? "" : "none";

    var personList = photoIdListField({
      label: "People",
      idKey: "person_ids",
      labelKey: "person_labels",
      idPlaceholder: "Paste person ID from Immich URL",
      labelPlaceholder: "Who is it?",
      addText: "Add a person",
      removeTitle: "Remove person ID",
      idChanges: { person: true, personLabel: true },
      labelChanges: { personLabel: true },
      clearChanges: { person: true, personLabel: true },
      onChange: function (changes, delayMs) { schedulePhotoSourceApply(delayMs, changes); }
    });
    var personField = personList.field;
    personField.style.display = S.photo_source === "Person" ? "" : "none";

    var tagList = photoIdListField({
      label: "Tags",
      idKey: "tag_ids",
      labelKey: "tag_labels",
      idPlaceholder: "Paste tag ID from Immich URL",
      labelPlaceholder: "What tag is it?",
      addText: "Add a tag",
      removeTitle: "Remove tag ID",
      idChanges: { tag: true, tagLabel: true },
      labelChanges: { tagLabel: true },
      clearChanges: { tag: true, tagLabel: true },
      onChange: function (changes, delayMs) { schedulePhotoSourceApply(delayMs, changes); }
    });
    var tagField = tagList.field;
    tagField.style.display = S.photo_source === "Tag" ? "" : "none";

    function validatePhotoSourceInputs(changes) {
      albumList.error.textContent = "";
      personList.error.textContent = "";
      tagList.error.textContent = "";
      var srcVal = srcSel.value;
      var albumTrim = albumList.getIdsValue();
      var albumLabels = albumList.getLabelsValue();
      var personTrim = personList.getIdsValue();
      var personLabels = personList.getLabelsValue();
      var tagTrim = tagList.getIdsValue();
      var tagLabels = tagList.getLabelsValue();
      var shouldValidateAlbum = changes.album || srcVal === "Album";
      var shouldValidatePerson = changes.person || srcVal === "Person";
      var shouldValidateTag = changes.tag || srcVal === "Tag";
      if (shouldValidateAlbum && photoIdFieldTooLong(albumTrim)) {
        albumList.error.textContent = PHOTO_ID_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidatePerson && photoIdFieldTooLong(personTrim)) {
        personList.error.textContent = PHOTO_ID_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidateTag && photoIdFieldTooLong(tagTrim)) {
        tagList.error.textContent = PHOTO_ID_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidateAlbum && !isValidUuidList(albumTrim)) {
        albumList.error.textContent = "Invalid UUID format";
        return null;
      }
      if (changes.albumLabel && photoLabelFieldTooLong(albumLabels)) {
        albumList.error.textContent = PHOTO_LABEL_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidatePerson && !isValidUuidList(personTrim)) {
        personList.error.textContent = "Invalid UUID format";
        return null;
      }
      if (changes.personLabel && photoLabelFieldTooLong(personLabels)) {
        personList.error.textContent = PHOTO_LABEL_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidateTag && !isValidUuidList(tagTrim)) {
        tagList.error.textContent = "Invalid UUID format";
        return null;
      }
      if (changes.tagLabel && photoLabelFieldTooLong(tagLabels)) {
        tagList.error.textContent = PHOTO_LABEL_FIELD_TOO_LONG;
        return null;
      }
      return {
        source: srcVal,
        albumOrder: S.album_order,
        albumIds: albumTrim,
        albumLabels: albumLabels,
        personIds: personTrim,
        personLabels: personLabels,
        tagIds: tagTrim,
        tagLabels: tagLabels
      };
    }
    function applyPhotoSourceInputs() {
      var changes = {
        source: pendingPhotoSourceSave.source,
        album: pendingPhotoSourceSave.album,
        albumLabel: pendingPhotoSourceSave.albumLabel,
        albumOrder: pendingPhotoSourceSave.albumOrder,
        person: pendingPhotoSourceSave.person,
        personLabel: pendingPhotoSourceSave.personLabel,
        tag: pendingPhotoSourceSave.tag,
        tagLabel: pendingPhotoSourceSave.tagLabel
      };
      pendingPhotoSourceSave = {
        source: false,
        album: false,
        albumLabel: false,
        albumOrder: false,
        person: false,
        personLabel: false,
        tag: false,
        tagLabel: false
      };
      var vals = validatePhotoSourceInputs(changes);
      if (!vals) return;
      var requests = [];
      if (changes.source) {
        requests.push(saveSetting("photo_source", vals.source));
      }
      if (changes.album) {
        requests.push(saveSetting("album_ids", vals.albumIds));
      }
      if (changes.albumLabel) {
        requests.push(saveSetting("album_labels", vals.albumLabels));
      }
      if (changes.albumOrder) {
        requests.push(saveSetting("album_order", vals.albumOrder));
      }
      if (changes.person) {
        requests.push(saveSetting("person_ids", vals.personIds));
      }
      if (changes.personLabel) {
        requests.push(saveSetting("person_labels", vals.personLabels));
      }
      if (changes.tag) {
        requests.push(saveSetting("tag_ids", vals.tagIds));
      }
      if (changes.tagLabel) {
        requests.push(saveSetting("tag_labels", vals.tagLabels));
      }
      if (!requests.length) return;
      Promise.all(requests).then(function () {
        if (changes.source || changes.album || changes.albumOrder || changes.person || changes.tag)
          post(endpoints.apply_photo_source + "/press");
      });
    }
    function schedulePhotoSourceApply(delayMs, changes) {
      if (changes) {
        pendingPhotoSourceSave.source = pendingPhotoSourceSave.source || !!changes.source;
        pendingPhotoSourceSave.album = pendingPhotoSourceSave.album || !!changes.album;
        pendingPhotoSourceSave.albumLabel = pendingPhotoSourceSave.albumLabel || !!changes.albumLabel;
        pendingPhotoSourceSave.albumOrder = pendingPhotoSourceSave.albumOrder || !!changes.albumOrder;
        pendingPhotoSourceSave.person = pendingPhotoSourceSave.person || !!changes.person;
        pendingPhotoSourceSave.personLabel = pendingPhotoSourceSave.personLabel || !!changes.personLabel;
        pendingPhotoSourceSave.tag = pendingPhotoSourceSave.tag || !!changes.tag;
        pendingPhotoSourceSave.tagLabel = pendingPhotoSourceSave.tagLabel || !!changes.tagLabel;
      }
      clearTimeout(photoSourceApplyTimer);
      photoSourceApplyTimer = setTimeout(applyPhotoSourceInputs, delayMs == null ? 600 : delayMs);
    }

    fSrc.appendChild(srcSel);
    srcBody.appendChild(fSrc);
    srcBody.appendChild(albumOrderField);
    srcBody.appendChild(albumField);
    srcBody.appendChild(personField);
    srcBody.appendChild(tagField);

    return makeCollapsibleCard("Photo Source", srcBody, true);

  }

  function makeAdvancedFiltersCard() {
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
    var filterDetails = el("div");
    filterDetails.style.display = S.date_filter_enabled ? "" : "none";
    filterBody.appendChild(toggleSettingRow({
      label: "Filter by Date",
      value: S.date_filter_enabled,
      getValue: function () { return S.date_filter_enabled; },
      setValue: function (value) { S.date_filter_enabled = value; },
      details: filterDetails,
      badge: filterBadge,
      badgeActive: function () { return isFilterActive(S.date_filter_enabled); },
      onChange: scheduleFilterApply
    }).field);

    var fFilterMode = field("Mode");
    var modeVal = S.date_filter_mode;
    var modeSegment = segmentedControl(productSettingOptions("date_filter_mode"), modeVal, function (v) {
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
    var relativeAmountMin = productNumberMin("relative_amount", 1);
    var relativeAmountMax = productNumberMax("relative_amount", 120);
    var relativeAmountStep = productNumberStep("relative_amount", 1);
    relativeAmountInput.type = "number";
    relativeAmountInput.min = String(relativeAmountMin);
    relativeAmountInput.max = String(relativeAmountMax);
    relativeAmountInput.step = String(relativeAmountStep);
    relativeAmountInput.value = String(S.relative_amount || 1);
    var relativeAmountError = el("div", "field-error");
    fRelativeAmount.appendChild(relativeAmountInput);
    fRelativeAmount.appendChild(relativeAmountError);
    relativeWrap.appendChild(fRelativeAmount);

    var fRelativeUnit = field("Unit");
    var relativeUnitSelect = selectFromOptions(productSettingOptions("relative_unit"), S.relative_unit, function () {
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
      if (S.date_filter_enabled && modeVal === "Relative Range" &&
          (!amountVal || amountVal < relativeAmountMin || amountVal > relativeAmountMax)) {
        relativeAmountError.textContent = "Enter a whole number from " + relativeAmountMin + " to " + relativeAmountMax;
        return null;
      }
      return { from: fromVal, to: toVal, amount: amountVal || relativeAmountMin, unit: unitVal };
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
        saveSetting("date_filter_enabled", S.date_filter_enabled),
        saveSetting("date_filter_mode", modeVal),
        saveSetting("date_from", vals.from),
        saveSetting("date_to", vals.to),
        saveSetting("relative_amount", vals.amount),
        saveSetting("relative_unit", vals.unit)
      ]).then(function () {
        post(endpoints.apply_photo_source + "/press");
      });
    }

    function scheduleFilterApply() {
      clearTimeout(filterApplyTimer);
      filterApplyTimer = setTimeout(applyFilterSettings, 300);
    }

    filterBody.appendChild(filterDetails);
    return makeCollapsibleCard("Advanced Filters", filterBody, true, filterBadge);
  }

  function makeLayoutCard() {
    // Layout
    var photoBody = el("div");

    var portraitRotationActive = isPortraitScreenRotation(effectiveScreenRotationForUi());
    var pairingEnabled = S.portrait_pairing && !portraitRotationActive;
    photoBody.appendChild(toggleSettingRow({
      label: "Portrait Pairing",
      value: pairingEnabled,
      getValue: function () { return S.portrait_pairing; },
      setValue: function (value) { S.portrait_pairing = value; },
      disabled: portraitRotationActive,
      disabledTitle: "Portrait pairing is disabled while the screen is in portrait rotation",
      onChange: function () {
        saveSetting("portrait_pairing", S.portrait_pairing);
      }
    }).field);

    var fPhotoOrientation = field("Photo Orientation");
    fPhotoOrientation.appendChild(
      selectFromOptions(productSettingOptions("photo_orientation"), S.photo_orientation, function (v) {
        saveSetting("photo_orientation", v);
      })
    );
    photoBody.appendChild(fPhotoOrientation);

    var fDisplayMode = field("Display Mode");
    fDisplayMode.appendChild(
      selectFromOptions(productSettingOptions("display_mode"), S.display_mode, function (v) {
        saveSetting("display_mode", v);
      })
    );
    photoBody.appendChild(fDisplayMode);

    return makeCollapsibleCard("Layout", photoBody, true);
  }

  function makeMetadataCard() {
    // Metadata
    function metadataIsActive() {
      return S.photo_metadata_date_enabled || S.photo_metadata_location_enabled;
    }
    var metadataBadge = makeBadge(metadataIsActive());
    var metadataBody = el("div");
    var metadataDateDetails = el("div");
    var fMetadataDateTakenFormat = null;

    function refreshMetadataDetails() {
      metadataDateDetails.style.display = S.photo_metadata_date_enabled ? "" : "none";
      if (fMetadataDateTakenFormat) {
        fMetadataDateTakenFormat.style.display =
          S.photo_metadata_date_enabled && S.photo_metadata_date_format === "Date Taken" ? "" : "none";
      }
      metadataBadge.className = "on-badge" + (metadataIsActive() ? " active" : "");
    }

    var fMetadataDate = toggleSettingRow({
      label: "Date",
      value: S.photo_metadata_date_enabled,
      getValue: function () { return S.photo_metadata_date_enabled; },
      setValue: function (value) { S.photo_metadata_date_enabled = value; },
      onChange: function () {
        refreshMetadataDetails();
        saveSetting("photo_metadata_date_enabled", S.photo_metadata_date_enabled);
      }
    }).field;

    var fMetadataDateFormat = field("Date Format");
    fMetadataDateFormat.appendChild(
      selectFromOptions(productSettingOptions("photo_metadata_date_format"), S.photo_metadata_date_format, function (v) {
        saveSetting("photo_metadata_date_format", v);
        refreshMetadataDetails();
      })
    );
    metadataDateDetails.appendChild(fMetadataDateFormat);

    fMetadataDateTakenFormat = field("Date Taken Format");
    fMetadataDateTakenFormat.appendChild(
      selectFromOptions(productSettingOptions("photo_metadata_date_taken_format"), S.photo_metadata_date_taken_format, function (v) {
        saveSetting("photo_metadata_date_taken_format", v);
      })
    );
    metadataDateDetails.appendChild(fMetadataDateTakenFormat);

    var fMetadataLocation = toggleSettingRow({
      label: "Location",
      value: S.photo_metadata_location_enabled,
      getValue: function () { return S.photo_metadata_location_enabled; },
      setValue: function (value) { S.photo_metadata_location_enabled = value; },
      onChange: function () {
        refreshMetadataDetails();
        saveSetting("photo_metadata_location_enabled", S.photo_metadata_location_enabled);
      }
    }).field;
    metadataBody.appendChild(fMetadataLocation);
    metadataBody.appendChild(fMetadataDate);
    metadataBody.appendChild(metadataDateDetails);

    refreshMetadataDetails();
    return makeCollapsibleCard("Metadata", metadataBody, true, metadataBadge);

  }
