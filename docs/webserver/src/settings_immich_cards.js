  function makeConnectionCard() {
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

    function showConnectionError(msg) {
      connStatus.innerHTML = '<span class="dot red"></span> ' + msg;
      clearTimeout(connStatus._t);
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

    var removeIdIcon = "<svg viewBox=\"0 0 24 24\" width=\"18\" height=\"18\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M3 6h18\"/><path d=\"M8 6V4h8v2\"/><path d=\"M19 6l-1 14H6L5 6\"/><path d=\"M10 11v5\"/><path d=\"M14 11v5\"/></svg>";
    var moveUpIcon = "<svg viewBox=\"0 0 24 24\" width=\"18\" height=\"18\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M12 19V5\"/><path d=\"M5 12l7-7 7 7\"/></svg>";
    var moveDownIcon = "<svg viewBox=\"0 0 24 24\" width=\"18\" height=\"18\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M12 5v14\"/><path d=\"M19 12l-7 7-7-7\"/></svg>";

    var albumOrderField = field("Album Order");
    albumOrderField.appendChild(
      selectFromOptions(productSettingOptions("album_order"), S.album_order, function (v) {
        S.album_order = v;
        schedulePhotoSourceApply(0, { albumOrder: true });
      })
    );
    albumOrderField.style.display = S.photo_source === "Album" ? "" : "none";

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
      Array.prototype.forEach.call(albumIdList.querySelectorAll(".album-id-up"), function (btn) {
        btn.disabled = albumInputs.length <= 1 || Number(btn.getAttribute("data-index")) === 0;
      });
      Array.prototype.forEach.call(albumIdList.querySelectorAll(".album-id-down"), function (btn) {
        btn.disabled = albumInputs.length <= 1 || Number(btn.getAttribute("data-index")) === albumInputs.length - 1;
      });
    }
    function syncAlbumMoveButtonIndexes() {
      Array.prototype.forEach.call(albumIdList.querySelectorAll(".photo-id-row"), function (row, index) {
        Array.prototype.forEach.call(row.querySelectorAll(".album-id-up,.album-id-down"), function (btn) {
          btn.setAttribute("data-index", String(index));
        });
      });
    }
    function moveAlbumIdRow(fromIndex, toIndex) {
      if (toIndex < 0 || toIndex >= albumInputs.length || fromIndex === toIndex) return;
      var row = albumInputs[fromIndex].closest(".photo-id-row");
      var targetRow = albumInputs[toIndex].closest(".photo-id-row");
      var movedInput = albumInputs.splice(fromIndex, 1)[0];
      var movedLabel = albumLabelInputs.splice(fromIndex, 1)[0];
      albumInputs.splice(toIndex, 0, movedInput);
      albumLabelInputs.splice(toIndex, 0, movedLabel);
      if (toIndex < fromIndex) albumIdList.insertBefore(row, targetRow);
      else albumIdList.insertBefore(row, targetRow.nextSibling);
      syncAlbumMoveButtonIndexes();
      refreshAlbumRemoveButtons();
      schedulePhotoSourceApply(0, { album: true, albumLabel: true });
    }
    function addAlbumIdRow(value, labelValue) {
      var row = el("div", "photo-id-row");
      var fields = el("div", "photo-id-fields");
      var albumInput = input("text", value || "", "Paste album ID from Immich URL", MAX_PHOTO_ID_FIELD_LENGTH);
      var albumLabelInput = input("text", labelValue || "", "What is it?", MAX_PHOTO_ID_FIELD_LENGTH);
      var actions = el("div", "photo-id-row-actions");
      var upBtn = el("button", "btn btn-secondary btn-icon album-id-up");
      upBtn.type = "button";
      upBtn.innerHTML = moveUpIcon;
      upBtn.title = "Move album up";
      upBtn.setAttribute("aria-label", "Move album up");
      upBtn.onclick = function () {
        var fromIndex = albumInputs.indexOf(albumInput);
        moveAlbumIdRow(fromIndex, fromIndex - 1);
      };
      var downBtn = el("button", "btn btn-secondary btn-icon album-id-down");
      downBtn.type = "button";
      downBtn.innerHTML = moveDownIcon;
      downBtn.title = "Move album down";
      downBtn.setAttribute("aria-label", "Move album down");
      downBtn.onclick = function () {
        var fromIndex = albumInputs.indexOf(albumInput);
        moveAlbumIdRow(fromIndex, fromIndex + 1);
      };
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
        syncAlbumMoveButtonIndexes();
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
      actions.appendChild(upBtn);
      actions.appendChild(downBtn);
      actions.appendChild(removeBtn);
      row.appendChild(actions);
      albumIdList.appendChild(row);
      albumInputs.push(albumInput);
      albumLabelInputs.push(albumLabelInput);
      syncAlbumMoveButtonIndexes();
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
      tagError.textContent = "";
      var srcVal = srcSel.value;
      var albumTrim = getAlbumIdsValue();
      var albumLabels = getAlbumLabelsValue();
      var personTrim = getPersonIdsValue();
      var personLabels = getPersonLabelsValue();
      var tagTrim = getTagIdsValue();
      var tagLabels = getTagLabelsValue();
      var shouldValidateAlbum = changes.album || srcVal === "Album";
      var shouldValidatePerson = changes.person || srcVal === "Person";
      var shouldValidateTag = changes.tag || srcVal === "Tag";
      if (shouldValidateAlbum && photoIdFieldTooLong(albumTrim)) {
        albumError.textContent = PHOTO_ID_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidatePerson && photoIdFieldTooLong(personTrim)) {
        personError.textContent = PHOTO_ID_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidateTag && photoIdFieldTooLong(tagTrim)) {
        tagError.textContent = PHOTO_ID_FIELD_TOO_LONG;
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
      if (shouldValidateTag && !isValidUuidList(tagTrim)) {
        tagError.textContent = "Invalid UUID format";
        return null;
      }
      if (changes.tagLabel && photoLabelFieldTooLong(tagLabels)) {
        tagError.textContent = PHOTO_LABEL_FIELD_TOO_LONG;
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

    var tagField = field("Tags");
    var tagIdList = el("div", "photo-id-list");
    var tagInputs = [];
    var tagLabelInputs = [];
    var tagError = el("div", "field-error");
    function getTagIdsValue() {
      return tagInputs.map(function (inputEl) {
        return inputEl.value.trim();
      }).filter(Boolean).join(",");
    }
    function getTagLabelsValue() {
      return buildPhotoLabelList(tagInputs, tagLabelInputs);
    }
    function refreshTagRemoveButtons() {
      Array.prototype.forEach.call(tagIdList.querySelectorAll(".tag-id-remove"), function (btn) {
        btn.disabled = tagInputs.length <= 1;
      });
    }
    function addTagIdRow(value, labelValue) {
      var row = el("div", "photo-id-row");
      var fields = el("div", "photo-id-fields");
      var tagInput = input("text", value || "", "Paste tag ID from Immich URL", MAX_PHOTO_ID_FIELD_LENGTH);
      var tagLabelInput = input("text", labelValue || "", "What tag is it?", MAX_PHOTO_ID_FIELD_LENGTH);
      var removeBtn = el("button", "btn btn-secondary btn-icon tag-id-remove");
      removeBtn.type = "button";
      removeBtn.innerHTML = removeIdIcon;
      removeBtn.title = "Remove tag ID";
      removeBtn.setAttribute("aria-label", "Remove tag ID");
      removeBtn.onclick = function () {
        if (tagInputs.length <= 1) {
          tagInput.value = "";
          tagLabelInput.value = "";
          schedulePhotoSourceApply(0, { tag: true, tagLabel: true });
          return;
        }
        var removeIndex = tagInputs.indexOf(tagInput);
        tagInputs.splice(removeIndex, 1);
        tagLabelInputs.splice(removeIndex, 1);
        row.parentNode.removeChild(row);
        refreshTagRemoveButtons();
        schedulePhotoSourceApply(0, { tag: true, tagLabel: true });
      };
      tagInput.oninput = function () {
        schedulePhotoSourceApply(null, { tag: true, tagLabel: true });
      };
      tagLabelInput.oninput = function () {
        schedulePhotoSourceApply(null, { tagLabel: true });
      };
      fields.appendChild(tagInput);
      fields.appendChild(tagLabelInput);
      row.appendChild(fields);
      row.appendChild(removeBtn);
      tagIdList.appendChild(row);
      tagInputs.push(tagInput);
      tagLabelInputs.push(tagLabelInput);
      refreshTagRemoveButtons();
    }
    var tagIds = splitPhotoIdList(S.tag_ids);
    var tagLabels = parsePhotoLabelList(S.tag_labels);
    for (var tagIndex = 0; tagIndex < Math.max(tagIds.length, tagLabels.length, 1); tagIndex++) {
      addTagIdRow(tagIds[tagIndex] || "", tagLabels[tagIndex] || "");
    }
    var addTagRow = el("div", "photo-id-actions");
    var addTagBtn = el("button", "btn btn-secondary");
    addTagBtn.type = "button";
    addTagBtn.textContent = "Add a tag";
    addTagBtn.title = "Add a tag";
    addTagBtn.setAttribute("aria-label", "Add a tag");
    addTagBtn.onclick = function () {
      addTagIdRow("", "");
      tagInputs[tagInputs.length - 1].focus();
    };
    addTagRow.appendChild(addTagBtn);
    tagField.appendChild(tagIdList);
    tagField.appendChild(addTagRow);
    tagField.appendChild(tagError);
    tagField.style.display = S.photo_source === "Tag" ? "" : "none";

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

    var fPairToggle = field("");
    var portraitRotationActive = isPortraitScreenRotation(effectiveScreenRotationForUi());
    var pairingEnabled = S.portrait_pairing && !portraitRotationActive;
    var pairTr = el("div", "toggle-row");
    pairTr.innerHTML = "<span>Portrait Pairing</span>";
    var pairTog = el("div", pairingEnabled ? "toggle on" : "toggle");
    if (portraitRotationActive) {
      pairTog.style.opacity = ".35";
      pairTog.style.cursor = "not-allowed";
      pairTog.title = "Portrait pairing is disabled while the screen is in portrait rotation";
    }
    pairTog.onclick = function () {
      if (portraitRotationActive) return;
      S.portrait_pairing = !S.portrait_pairing;
      pairTog.className = S.portrait_pairing ? "toggle on" : "toggle";
      saveSetting("portrait_pairing", S.portrait_pairing);
    };
    pairTr.appendChild(pairTog);
    fPairToggle.appendChild(pairTr);
    photoBody.appendChild(fPairToggle);

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

    var fMetadataDate = field("");
    var metadataDateTr = el("div", "toggle-row");
    metadataDateTr.innerHTML = "<span>Date</span>";
    var metadataDateTog = el("div", S.photo_metadata_date_enabled ? "toggle on" : "toggle");
    metadataDateTog.onclick = function () {
      S.photo_metadata_date_enabled = !S.photo_metadata_date_enabled;
      metadataDateTog.className = S.photo_metadata_date_enabled ? "toggle on" : "toggle";
      refreshMetadataDetails();
      saveSetting("photo_metadata_date_enabled", S.photo_metadata_date_enabled);
    };
    metadataDateTr.appendChild(metadataDateTog);
    fMetadataDate.appendChild(metadataDateTr);

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

    var fMetadataLocation = field("");
    var metadataLocationTr = el("div", "toggle-row");
    metadataLocationTr.innerHTML = "<span>Location</span>";
    var metadataLocationTog = el("div", S.photo_metadata_location_enabled ? "toggle on" : "toggle");
    metadataLocationTog.onclick = function () {
      S.photo_metadata_location_enabled = !S.photo_metadata_location_enabled;
      metadataLocationTog.className = S.photo_metadata_location_enabled ? "toggle on" : "toggle";
      refreshMetadataDetails();
      saveSetting("photo_metadata_location_enabled", S.photo_metadata_location_enabled);
    };
    metadataLocationTr.appendChild(metadataLocationTog);
    fMetadataLocation.appendChild(metadataLocationTr);
    metadataBody.appendChild(fMetadataLocation);
    metadataBody.appendChild(fMetadataDate);
    metadataBody.appendChild(metadataDateDetails);

    refreshMetadataDetails();
    return makeCollapsibleCard("Metadata", metadataBody, true, metadataBadge);

  }
