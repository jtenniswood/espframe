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
      keyWrap.replaceChildren();
      keyWrap.appendChild(makeMaskedApiKeyRow(function () {
        keyWrap.replaceChildren();
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

  function makeSmartPhotoFilterCard() {
    var body = el("div");
    var version = String(S.immich_server_version || "Unknown");
    var parts = version.split(".").map(Number);
    var supportsStructured = parts.length >= 2 && isFinite(parts[0]) && isFinite(parts[1]) &&
      (parts[0] > 3 || (parts[0] === 3 && parts[1] >= 2));
    var capability = el("div", supportsStructured ? "setting-hint" : "banner warning");
    capability.textContent = supportsStructured
      ? "Immich " + version + " · structured filters available"
      : "Immich " + version + " · Some filters require Immich server version 3.2 or newer.";
    body.appendChild(capability);

    if (S.memories_migration_notice) {
      var notice = el("div", "banner warning");
      notice.textContent = "Memories was replaced by an empty filter (All Photos). Use date rules to create a similar playlist. ";
      var dismiss = button("Dismiss", "btn btn-secondary", function () {
        post(endpoints.memories_migration_notice + "/turn_off").then(function () {
          S.memories_migration_notice = false;
          notice.style.display = "none";
        });
      });
      notice.appendChild(dismiss);
      body.appendChild(notice);
    }

    function applySetting(key, value) {
      return saveSetting(key, value, { applyPhotoSource: true });
    }
    function addSelect(label, key, disabled, reason, recoveryValue) {
      var f = field(label);
      var control = selectFromOptions(productSettingOptions(key), S[key], function (value) {
        S[key] = value;
        applySetting(key, value);
      });
      if (disabled && recoveryValue != null) {
        Array.prototype.forEach.call(control.options, function (optionEl) {
          optionEl.disabled = String(optionEl.value) !== String(recoveryValue);
        });
      }
      f.appendChild(control);
      if (disabled && reason) {
        var hint = el("div", "setting-hint");
        hint.textContent = reason;
        f.appendChild(hint);
      }
      body.appendChild(f);
      return f;
    }
    function saveList(editor, idKey, labelKey) {
      var ids = editor.getIdsValue();
      var labels = editor.getLabelsValue();
      editor.error.textContent = "";
      if (photoIdFieldTooLong(ids) || photoLabelFieldTooLong(labels)) {
        editor.error.textContent = "List exceeds the 255-character device limit.";
        return;
      }
      if (ids && !isValidUuidList(ids)) {
        editor.error.textContent = "Invalid UUID format";
        return;
      }
      S[idKey] = ids;
      S[labelKey] = labels;
      Promise.all([saveSetting(idKey, ids), saveSetting(labelKey, labels)]).then(function () {
        post(endpoints.apply_photo_source + "/press");
      });
    }
    function addExclusions(parent, label, idKey, labelKey, noun) {
      var nested = document.createElement("details");
      nested.className = "filter-nested filter-exclusions";
      nested.open = !!String(S[idKey] || "").trim();
      var summary = document.createElement("summary");
      summary.textContent = label;
      nested.appendChild(summary);
      var timer = null;
      var editor = photoIdListField({
        label: "", idKey: idKey, labelKey: labelKey,
        idPlaceholder: "Paste excluded " + noun + " UUID", labelPlaceholder: "Optional label",
        addText: "Exclude " + noun, removeTitle: "Remove exclusion",
        moveUpTitle: "Move up", moveDownTitle: "Move down",
        disableEditing: !supportsStructured, allowClearLast: !supportsStructured,
        idChanges: {}, labelChanges: {}, clearChanges: {}, reorderChanges: {},
        onChange: function (_changes, delayMs) {
          clearTimeout(timer);
          timer = setTimeout(function () { saveList(editor, idKey, labelKey); }, delayMs == null ? 600 : delayMs);
        }
      });
      nested.appendChild(editor.field);
      if (!supportsStructured) {
        var hint = el("div", "setting-hint compatibility-disabled");
        hint.textContent = "Requires Immich server version 3.2 or newer. Saved exclusions can be removed.";
        nested.appendChild(hint);
      }
      parent.appendChild(nested);
    }

    function addGroup(label, enabledKey, idKey, labelKey, noun, options) {
      var details = el("div", "filter-group-details");
      var row = toggleSettingRow({
        label: "Filter by " + label, value: !!S[enabledKey],
        getValue: function () { return !!S[enabledKey]; },
        setValue: function (value) { S[enabledKey] = value; }, details: details,
        onChange: function (value) { applySetting(enabledKey, value); }
      });
      body.appendChild(row.field);
      var inclusionParent = details;
      if (options && options.includedPanel) {
        var included = document.createElement("details");
        included.className = "filter-nested filter-inclusions";
        included.open = true;
        var includedSummary = document.createElement("summary");
        includedSummary.textContent = "Included " + label;
        included.appendChild(includedSummary);
        details.appendChild(included);
        inclusionParent = included;
      }
      if (!options || options.showInclusionHint !== false) {
        var hint = el("div", "setting-hint");
        hint.textContent = "Any selected " + noun + " is included.";
        inclusionParent.appendChild(hint);
      }
      var timer = null;
      var editor = photoIdListField({
        label: "Selected " + label, idKey: idKey, labelKey: labelKey,
        idPlaceholder: "Paste " + noun + " UUID from Immich", labelPlaceholder: "Optional label",
        addText: "Add " + noun, removeTitle: "Remove " + noun,
        moveUpTitle: "Move up", moveDownTitle: "Move down",
        idChanges: {}, labelChanges: {}, clearChanges: {}, reorderChanges: {},
        onChange: function (_changes, delayMs) {
          clearTimeout(timer);
          timer = setTimeout(function () { saveList(editor, idKey, labelKey); }, delayMs == null ? 600 : delayMs);
        }
      });
      inclusionParent.appendChild(editor.field);
      addExclusions(details, "Excluded " + label, options.excludedIdKey, options.excludedLabelKey, noun);
      if (options && options.order) details.appendChild(productSelectSettingField("Album Order", "album_order"));
      details.style.display = S[enabledKey] ? "" : "none";
      body.appendChild(details);
    }

    addGroup("Albums", "albums_enabled", "album_ids", "album_labels", "album", {
      order: true, excludedIdKey: "excluded_album_ids", excludedLabelKey: "excluded_album_labels", includedPanel: true
    });
    addGroup("People", "people_enabled", "person_ids", "person_labels", "person", {
      excludedIdKey: "excluded_person_ids", excludedLabelKey: "excluded_person_labels", includedPanel: true, showInclusionHint: false
    });
    addGroup("Tags", "tags_enabled", "tag_ids", "tag_labels", "tag", {
      excludedIdKey: "excluded_tag_ids", excludedLabelKey: "excluded_tag_labels", includedPanel: true
    });

    var configuredGroups = ["album_ids", "person_ids", "tag_ids"].filter(function (key) {
      return !!String(S[key] || "").trim();
    }).length;
    if (configuredGroups > 1) {
      var advanced = document.createElement("details");
      advanced.className = "filter-nested";
      var advancedSummary = document.createElement("summary");
      advancedSummary.textContent = "Advanced inclusion options";
      advanced.appendChild(advancedSummary);
      advanced.appendChild(addSelect("Inclusion Groups", "inclusion_matching", false, ""));
      body.appendChild(advanced);
    }

    function addValueGroup(label, enabledKey, settingKey, defaultValue, disabled, reason) {
      var details = el("div", "filter-group-details");
      var row = toggleSettingRow({
        label: "Filter by " + label, value: !!S[enabledKey],
        getValue: function () { return !!S[enabledKey]; },
        setValue: function (value) { S[enabledKey] = value; }, details: details,
        onChange: function (value) {
          if (value && (S[settingKey] == null || S[settingKey] === "Any")) {
            S[settingKey] = defaultValue;
            saveSetting(settingKey, defaultValue);
          }
          applySetting(enabledKey, value);
        }
      });
      body.appendChild(row.field);
      details.appendChild(addSelect(label, settingKey, disabled, reason, "Any"));
      details.style.display = S[enabledKey] ? "" : "none";
      body.appendChild(details);
    }
    addValueGroup("Favorites", "favorites_enabled", "favorite_mode", "Favorites only", false, "");
    addValueGroup("Rating", "rating_enabled", "minimum_rating", "1+", !supportsStructured,
      "Requires Immich server version 3.2 or newer. Choose Any to clear.");

    var locationDetails = el("div", "filter-group-details");
    var locationRow = toggleSettingRow({
      label: "Filter by Location", value: !!S.location_enabled,
      getValue: function () { return !!S.location_enabled; },
      setValue: function (value) { S.location_enabled = value; }, details: locationDetails,
      onChange: function (value) { applySetting("location_enabled", value); }
    });
    body.appendChild(locationRow.field);
    [["Country", "filter_country"], ["State / Province", "filter_state"], ["City", "filter_city"]].forEach(function (spec, index) {
      var f = field(spec[0]);
      var inputEl = input("text", S[spec[1]] || "", "Exact Immich value", productTextMaxLength(spec[1], 96));
      var hint = el("div", "setting-hint");
      if (index === 1) hint.textContent = "Enter a country first.";
      if (index === 2) hint.textContent = "Enter a country and state or province first.";
      var timer = null;
      inputEl.oninput = function () {
        var nextValue = inputEl.value.trim();
        var parentValue = index === 1 ? S.filter_country : S.filter_state;
        if (nextValue && index > 0 && !String(parentValue || "").trim()) {
          inputEl.setCustomValidity(index === 1 ? "Enter a country first" : "Enter a state or province first");
          return;
        }
        inputEl.setCustomValidity("");
        clearTimeout(timer);
        timer = setTimeout(function () { S[spec[1]] = nextValue; applySetting(spec[1], nextValue); }, 600);
      };
      f.appendChild(inputEl); f.appendChild(hint); locationDetails.appendChild(f);
    });
    locationDetails.style.display = S.location_enabled ? "" : "none";
    body.appendChild(locationDetails);
    return makeCollapsibleCard("Photo Filter", body, true);
  }

  function makePhotoSourceCard() {
    return makeSmartPhotoFilterCard();
  }

  function makeLegacyPhotoSourceCard() {
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
      tagLabel: false,
      tagMatching: false
    };
    var fSrc = field("Source");
    var srcSel = selectFromOptions(productSettingOptions("photo_source"), S.photo_source, function (v) {
      S.photo_source = v;
      albumField.style.display = v === "Album" ? "" : "none";
      albumOrderField.style.display = v === "Album" ? "" : "none";
      personField.style.display = v === "Person" ? "" : "none";
      tagField.style.display = v === "Tag" ? "" : "none";
      tagMatchingField.style.display = v === "Tag" ? "" : "none";
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
      moveUpTitle: "Move album up",
      moveDownTitle: "Move album down",
      idChanges: { album: true, albumLabel: true },
      labelChanges: { albumLabel: true },
      clearChanges: { album: true, albumLabel: true },
      reorderChanges: { album: true, albumLabel: true },
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
      moveUpTitle: "Move person up",
      moveDownTitle: "Move person down",
      idChanges: { person: true, personLabel: true },
      labelChanges: { personLabel: true },
      clearChanges: { person: true, personLabel: true },
      reorderChanges: { person: true, personLabel: true },
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
      moveUpTitle: "Move tag up",
      moveDownTitle: "Move tag down",
      idChanges: { tag: true, tagLabel: true },
      labelChanges: { tagLabel: true },
      clearChanges: { tag: true, tagLabel: true },
      reorderChanges: { tag: true, tagLabel: true },
      onChange: function (changes, delayMs) { schedulePhotoSourceApply(delayMs, changes); }
    });
    var tagField = tagList.field;
    tagField.style.display = S.photo_source === "Tag" ? "" : "none";

    var tagMatchingField = field("Tag Matching");
    tagMatchingField.appendChild(
      selectFromOptions(productSettingOptions("tag_matching"), S.tag_matching, function (v) {
        S.tag_matching = v;
        schedulePhotoSourceApply(0, { tagMatching: true });
      })
    );
    tagMatchingField.style.display = S.photo_source === "Tag" ? "" : "none";

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
      if (srcVal === "Album" && !albumTrim) {
        albumList.error.textContent = "Add at least one album";
        return null;
      }
      if (srcVal === "Person" && !personTrim) {
        personList.error.textContent = "Add at least one person";
        return null;
      }
      if (srcVal === "Tag" && !tagTrim) {
        tagList.error.textContent = "Add at least one tag";
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
        tagLabels: tagLabels,
        tagMatching: S.tag_matching
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
        tagLabel: pendingPhotoSourceSave.tagLabel,
        tagMatching: pendingPhotoSourceSave.tagMatching
      };
      var vals = validatePhotoSourceInputs(changes);
      // Keep the complete pending change set when validation fails. A user can
      // select Album/Person/Tag before adding its first ID; the later valid ID
      // edit must save both the ID and that original source selection.
      if (!vals) return;
      pendingPhotoSourceSave = {
        source: false,
        album: false,
        albumLabel: false,
        albumOrder: false,
        person: false,
        personLabel: false,
        tag: false,
        tagLabel: false,
        tagMatching: false
      };
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
      if (changes.tagMatching) {
        requests.push(saveSetting("tag_matching", vals.tagMatching));
      }
      if (!requests.length) return;
      Promise.all(requests).then(function () {
        if (changes.source || changes.album || changes.albumOrder || changes.person ||
            changes.tag || changes.tagMatching)
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
        pendingPhotoSourceSave.tagMatching = pendingPhotoSourceSave.tagMatching || !!changes.tagMatching;
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
    srcBody.appendChild(tagMatchingField);

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
    dateFromInput.maxLength = productTextMaxLength("date_from", 10);
    var dateFromError = el("div", "field-error");
    fDateFrom.appendChild(dateFromInput);
    fDateFrom.appendChild(dateFromError);
    fixedWrap.appendChild(fDateFrom);

    var fDateTo = field("Until");
    var dateToInput = document.createElement("input");
    dateToInput.type = "date";
    dateToInput.value = S.date_to || "";
    dateToInput.placeholder = "YYYY-MM-DD";
    dateToInput.maxLength = productTextMaxLength("date_to", 10);
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

  function makePortraitPairingCard() {
    var pairingBody = el("div");
    var portraitRotationActive = isPortraitScreenRotation(effectiveScreenRotationForUi());
    var pairingEnabled = S.portrait_pairing && !portraitRotationActive;
    var pairingOptionsBody = el("div", "portrait-pairing-options");
    var pairingBadge = makeBadge(pairingEnabled);
    var pairingToggle = toggleSettingRow({
      label: "Portrait Pairing",
      value: pairingEnabled,
      getValue: function () { return S.portrait_pairing; },
      setValue: function (value) { S.portrait_pairing = value; },
      disabled: portraitRotationActive,
      disabledTitle: "Portrait pairing is disabled while the screen is in portrait rotation",
      onChange: function () {
        pairingOptionsBody.style.display = S.portrait_pairing && !portraitRotationActive ? "" : "none";
        setBadgeActive(pairingBadge, S.portrait_pairing && !portraitRotationActive);
        saveSetting("portrait_pairing", S.portrait_pairing);
      }
    });

    var pairingOptionsDisabledTitle = portraitRotationActive
      ? "Portrait pairing is disabled while the screen is in portrait rotation"
      : "Turn on Portrait Pairing to use this option";

    pairingOptionsBody.appendChild(toggleSettingRow({
      label: "Show Paired Portraits Only",
      value: S.portrait_pairs_only,
      getValue: function () { return S.portrait_pairs_only; },
      setValue: function (value) { S.portrait_pairs_only = value; },
      disabled: portraitRotationActive,
      disabledTitle: pairingOptionsDisabledTitle,
      onChange: function () {
        saveSetting("portrait_pairs_only", S.portrait_pairs_only);
      }
    }).field);

    var fPairingRange = field("Pairing Range");
    var pairingRangeSelect = selectFromOptions(
      productSettingOptions("portrait_pairing_range"),
      S.portrait_pairing_range,
      function (v) { saveSetting("portrait_pairing_range", v); },
      function (v) {
        if (v === "Within 1 Day") return "±1 Day";
        if (v === "Within 2 Days") return "±2 Days";
        return v;
      }
    );
    pairingRangeSelect.disabled = portraitRotationActive;
    if (portraitRotationActive) pairingRangeSelect.title = pairingOptionsDisabledTitle;
    fPairingRange.appendChild(pairingRangeSelect);
    pairingOptionsBody.appendChild(fPairingRange);

    pairingBody.appendChild(pairingToggle.field);
    pairingOptionsBody.style.display = pairingEnabled ? "" : "none";
    pairingBody.appendChild(pairingOptionsBody);
    var pairingCard = makeCollapsibleCard("Portrait Pairing", pairingBody, false, pairingBadge);
    return pairingCard;
  }

  function makeLayoutCard() {
    var photoBody = el("div");

    var fPhotoOrientation = field("Display Photos");
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
      }, function (v) {
        return v === "Fill" ? "Crop to fit" : "Show full image";
      })
    );
    photoBody.appendChild(fDisplayMode);

    return makeCollapsibleCard("Photo Display", photoBody, true);
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
