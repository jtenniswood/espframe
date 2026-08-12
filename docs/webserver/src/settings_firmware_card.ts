  function parseFirmwareVersion(value) {
    var match = /^v([0-9]+)\.([0-9]+)\.([0-9]+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$/i.exec(String(value || "").trim());
    if (!match) return null;
    return {
      core: [Number(match[1]), Number(match[2]), Number(match[3])],
      prerelease: match[4] ? match[4].split(".") : []
    };
  }

  function isSpecificFirmwareVersion(value) {
    return !!parseFirmwareVersion(value);
  }

  function compareFirmwareVersions(left, right) {
    var a = parseFirmwareVersion(left);
    var b = parseFirmwareVersion(right);
    if (!a || !b) return null;
    for (var i = 0; i < a.core.length; i++) {
      if (a.core[i] !== b.core[i]) return a.core[i] > b.core[i] ? 1 : -1;
    }
    if (!a.prerelease.length || !b.prerelease.length) {
      if (a.prerelease.length === b.prerelease.length) return 0;
      return a.prerelease.length ? -1 : 1;
    }
    var length = Math.max(a.prerelease.length, b.prerelease.length);
    for (var index = 0; index < length; index++) {
      if (a.prerelease[index] === undefined) return -1;
      if (b.prerelease[index] === undefined) return 1;
      if (a.prerelease[index] === b.prerelease[index]) continue;
      var aNumeric = /^[0-9]+$/.test(a.prerelease[index]);
      var bNumeric = /^[0-9]+$/.test(b.prerelease[index]);
      if (aNumeric && bNumeric) return Number(a.prerelease[index]) > Number(b.prerelease[index]) ? 1 : -1;
      if (aNumeric !== bNumeric) return aNumeric ? -1 : 1;
      return a.prerelease[index] > b.prerelease[index] ? 1 : -1;
    }
    return 0;
  }

  function firmwareVersionsSame(a, b) {
    return String(a || "").trim().toLowerCase() === String(b || "").trim().toLowerCase();
  }

  function installedFirmwareVersion() {
    return String(S.firmware || S.installed_version || "").trim();
  }

  function firmwareDeviceSlug() {
    return String(S.firmware_device || "").trim();
  }

  function firmwarePublicManifestUrl() {
    var slug = firmwareDeviceSlug();
    var devices = FIRMWARE_MANIFEST_URLS && FIRMWARE_MANIFEST_URLS.devices;
    if (slug && devices && devices[slug] && devices[slug].stable) return devices[slug].stable;
    if (!devices || Object.keys(devices).length <= 1) return FIRMWARE_MANIFEST_URLS.stable || "";
    return "";
  }

  function firmwarePublicVersionsUrl() {
    var manifestUrl = firmwarePublicManifestUrl();
    return manifestUrl ? new URL("versions.json", manifestUrl).href : "";
  }

  function firmwarePublicAssetUrl(path, baseUrl) {
    try {
      var base = new URL(baseUrl);
      var resolved = new URL(String(path || ""), base);
      var baseDirectory = base.pathname.slice(0, base.pathname.lastIndexOf("/") + 1);
      if (resolved.origin !== base.origin || resolved.pathname.indexOf(baseDirectory) !== 0) return "";
      return resolved.href;
    } catch (_) {
      return "";
    }
  }

  function firmwareInfoFromVersionEntry(entry) {
    if (!entry || typeof entry !== "object") return null;
    var version = String(entry.version || "").trim();
    var ota = entry.ota && typeof entry.ota === "object" ? entry.ota : {};
    var otaPath = String(ota.path || "").trim();
    var slug = firmwareDeviceSlug();
    var expectedFilename = slug + ".ota.bin";
    if (!slug) return null;
    if (!/^v[0-9]+(\.[0-9]+){2}$/i.test(version) || !otaPath || otaPath.split("/").pop() !== expectedFilename) return null;
    var otaUrl = firmwarePublicAssetUrl(otaPath, firmwarePublicVersionsUrl());
    if (!otaUrl) return null;
    return {
      version: version,
      release_url: String(entry.release_url || ota.release_url || "").trim(),
      ota_url: otaUrl,
      ota_filename: expectedFilename,
      ota_md5: String(ota.md5 || "").trim()
    };
  }

  function firmwareInfosFromVersionsIndex(data) {
    if (!data || typeof data !== "object" || data.device !== firmwareDeviceSlug() || !Array.isArray(data.versions)) return [];
    var seen = {};
    var infos = [];
    data.versions.some(function (entry) {
      var info = firmwareInfoFromVersionEntry(entry);
      var key = info && info.version.toLowerCase();
      if (!info || seen[key]) return false;
      seen[key] = true;
      infos.push(info);
      return infos.length >= 5;
    });
    return infos;
  }

  function previousFirmwareInfos() {
    var installed = installedFirmwareVersion();
    var latest = S.firmware_version_options && S.firmware_version_options.length
      ? S.firmware_version_options[0].version
      : S.latest_version;
    var latestIsUpdate = compareFirmwareVersions(latest, installed) > 0;
    return (S.firmware_version_options || []).filter(function (info) {
      if (firmwareVersionsSame(info.version, installed)) return false;
      return !latestIsUpdate || !firmwareVersionsSame(info.version, latest);
    });
  }

  function selectedPreviousFirmwareInfo() {
    var infos = previousFirmwareInfos();
    for (var i = 0; i < infos.length; i++) {
      if (firmwareVersionsSame(infos[i].version, S.firmware_selected_version)) return infos[i];
    }
    return infos.length ? infos[0] : null;
  }

  function latestFirmwareInfo() {
    return S.firmware_version_options && S.firmware_version_options.length ? S.firmware_version_options[0] : null;
  }

  function firmwareUpdateKnownAvailable() {
    var installed = installedFirmwareVersion();
    var latest = String(S.latest_version || "").trim();
    return !!S.update_available || compareFirmwareVersions(latest, installed) > 0;
  }

  function c6FirmwareUpdateKnownAvailable() {
    var current = String(S.c6_current_firmware || "").trim();
    var latest = String(S.c6_available_firmware || "").trim();
    return /\d/.test(current) && /\d/.test(latest) && !firmwareVersionsSame(current, latest);
  }

  function refreshPreviousFirmwareUi() {
    if (!els.fwPreviousPanel || !els.fwVersionSelect) return;
    var infos = previousFirmwareInfos();
    var currentOptions = Array.from(els.fwVersionSelect.options).map(function (option) { return option.value; });
    var nextOptions = infos.map(function (info) { return info.version; });
    if (currentOptions.join("|") !== nextOptions.join("|")) {
      els.fwVersionSelect.replaceChildren();
      infos.forEach(function (info) {
        var option = document.createElement("option");
        option.value = info.version;
        option.textContent = info.version;
        els.fwVersionSelect.appendChild(option);
      });
    }
    var selected = selectedPreviousFirmwareInfo();
    S.firmware_selected_version = selected ? selected.version : "";
    els.fwVersionSelect.value = S.firmware_selected_version;
    els.fwPreviousPanel.style.display = S.firmware_versions_loaded && infos.length ? "" : "none";
    var busy = !!(S.firmware_checking || S.firmware_installing || S.firmware_uploading);
    els.fwVersionSelect.disabled = busy;
    if (els.fwPreviousInstallBtn) {
      els.fwPreviousInstallBtn.disabled = busy || !selected;
      if (S.firmware_uploading) els.fwPreviousInstallBtn.textContent = "Uploading\u2026";
      else if (S.firmware_installing) els.fwPreviousInstallBtn.textContent = "Installing\u2026";
      else els.fwPreviousInstallBtn.textContent = "Install";
    }
  }

  function refreshFirmwareUi() {
    if (els.fwCurrentVersion) els.fwCurrentVersion.textContent = displayVersion(installedFirmwareVersion(), "Dev");
    if (els.fwLatestVersion) {
      els.fwLatestVersion.textContent = isSpecificFirmwareVersion(S.latest_version)
        ? S.latest_version
        : (S.firmware_checking ? "Checking\u2026" : "Not checked");
    }
    var available = firmwareUpdateKnownAvailable();
    setDisclosureBadgeActive(els.firmwareUpdatesBadge, available);
    setDisclosureBadgeActive(els.autoUpdateBadge, !!S.auto_update);
    setBadgeActive(els.firmwareCardBadge, available || c6FirmwareUpdateKnownAvailable());
    if (els.fwAutoToggle) els.fwAutoToggle.className = S.auto_update ? "toggle on" : "toggle";
    if (els.fwFrequencyField) els.fwFrequencyField.style.display = S.auto_update ? "" : "none";
    if (els.fwStatus) {
      els.fwStatus.className = "fw-status" + (S.firmware_install_error ? " error" : "");
      els.fwStatus.textContent = S.firmware_install_error || "";
    }
    if (els.fwActionBtn) {
      var busy = !!(S.firmware_checking || S.firmware_installing || S.firmware_uploading);
      els.fwActionBtn.disabled = busy;
      if (S.firmware_uploading) els.fwActionBtn.textContent = "Uploading\u2026";
      else if (S.firmware_installing) els.fwActionBtn.textContent = "Installing\u2026";
      else if (S.firmware_checking) els.fwActionBtn.textContent = "Checking\u2026";
      else els.fwActionBtn.textContent = available ? "Install Update" : "Check for Update";
    }
    refreshPreviousFirmwareUi();
  }

  function refreshC6FirmwareUi() {
    if (els.c6FirmwareCurrent) els.c6FirmwareCurrent.textContent = displayVersion(S.c6_current_firmware, "Unknown");
    if (els.c6FirmwareLatest) els.c6FirmwareLatest.textContent = displayVersion(S.c6_available_firmware, "Unknown");
    var available = c6FirmwareUpdateKnownAvailable();
    setDisclosureBadgeActive(els.c6FirmwareBadge, available);
    setBadgeActive(els.firmwareCardBadge, firmwareUpdateKnownAvailable() || available);
    if (els.c6AutoToggle) els.c6AutoToggle.className = S.c6_auto_update ? "toggle on" : "toggle";
    if (els.c6FirmwareStatus) {
      var status = String(S.c6_update_status || "");
      if (S.c6_firmware_installing) els.c6FirmwareStatus.textContent = "Installing\u2026";
      else if (S.c6_firmware_checking) els.c6FirmwareStatus.textContent = "Checking\u2026";
      else els.c6FirmwareStatus.textContent = (!available || /^Could not/.test(status)) && status && status !== "Unknown" ? status : "";
    }
    if (els.c6FirmwareActionBtn) {
      var busy = !!(S.c6_firmware_checking || S.c6_firmware_installing);
      els.c6FirmwareActionBtn.disabled = busy;
      if (S.c6_firmware_installing) els.c6FirmwareActionBtn.textContent = "Installing\u2026";
      else if (S.c6_firmware_checking) els.c6FirmwareActionBtn.textContent = "Checking\u2026";
      else els.c6FirmwareActionBtn.textContent = available ? "Update WiFi Firmware" : "Check for Update";
    }
  }

  function fetchPublicFirmwareVersions() {
    if (S.firmware_versions_loading) return Promise.resolve(S.firmware_version_options || []);
    var versionsUrl = firmwarePublicVersionsUrl();
    if (!versionsUrl) return Promise.resolve([]);
    S.firmware_versions_loading = true;
    return fetch(versionsUrl, { cache: "no-store" })
      .then(function (response) {
        if (!response.ok) throw new Error("version_index_unavailable");
        return response.json();
      })
      .then(function (data) {
        var infos = firmwareInfosFromVersionsIndex(data);
        S.firmware_version_options = infos;
        S.firmware_versions_loaded = true;
        if (infos.length) S.latest_version = infos[0].version;
        refreshFirmwareUi();
        return infos;
      })
      .catch(function () {
        S.firmware_version_options = [];
        S.firmware_versions_loaded = true;
        refreshFirmwareUi();
        return [];
      })
      .finally(function () { S.firmware_versions_loading = false; });
  }

  function applyFirmwareUpdateResponse(data) {
    if (!data) return false;
    if (data.current_version) S.installed_version = String(data.current_version);
    if (data.latest_version || data.value) S.latest_version = String(data.latest_version || data.value);
    S.update_available = data.state === "UPDATE AVAILABLE" ||
      compareFirmwareVersions(S.latest_version, installedFirmwareVersion()) > 0;
    refreshFirmwareUi();
    return S.update_available;
  }

  function markFirmwareRestartPending() {
    S.firmware_uploading = false;
    S.firmware_installing = true;
    S.firmware_restart_pending = true;
    showBanner("Firmware uploaded. Waiting for the display to restart\u2026", "success");
    refreshFirmwareUi();
  }

  function failFirmwareInstall(message) {
    S.firmware_uploading = false;
    S.firmware_installing = false;
    S.firmware_install_error = message || "Firmware update failed.";
    showBanner(S.firmware_install_error, "error");
    refreshFirmwareUi();
  }

  function installPublicFirmware(info) {
    if (!info || !info.ota_url || S.firmware_uploading || S.firmware_installing) return Promise.resolve(false);
    S.firmware_install_error = "";
    S.firmware_uploading = true;
    refreshFirmwareUi();
    var uploadStarted = false;
    var uploadResponseReceived = false;
    return fetch(info.ota_url, { cache: "no-store" })
      .then(function (response) {
        if (!response.ok) throw new Error("Could not download firmware file (" + response.status + ").");
        return response.blob();
      })
      .then(function (blob) {
        return post(endpoints.firmware_prepare_upload + "/press").then(function () { return blob; });
      })
      .then(function (blob) {
        var form = new FormData();
        form.append("file", blob, info.ota_filename);
        uploadStarted = true;
        return fetch("/update", { method: "POST", body: form });
      })
      .then(function (response) {
        uploadResponseReceived = true;
        return response.text().catch(function () { return ""; }).then(function (responseText) {
          if (!response.ok) throw new Error("Device rejected firmware upload (" + response.status + ").");
          if (/update failed/i.test(responseText)) throw new Error("The display reported that the firmware upload failed.");
          markFirmwareRestartPending();
          return true;
        });
      })
      .catch(function (error) {
        if (uploadStarted && !uploadResponseReceived) {
          markFirmwareRestartPending();
          return true;
        }
        var message = error && error.message ? error.message : "Could not upload firmware update.";
        if (!uploadResponseReceived) {
          failFirmwareInstall(message);
          return false;
        }
        return post(endpoints.firmware_cancel_upload + "/press")
          .catch(function () {
            message += " The display's update recovery state could not be cleared; restart it before trying again.";
          })
          .then(function () {
            failFirmwareInstall(message);
            return false;
          });
      });
  }

  function startFirmwareInstall() {
    if (!firmwareUpdateKnownAvailable()) return;
    S.firmware_install_error = "";
    S.firmware_installing = true;
    refreshFirmwareUi();
    post(endpoints.update + "/install")
      .then(function () { S.firmware_restart_pending = true; })
      .catch(function () {
        var info = latestFirmwareInfo();
        S.firmware_installing = false;
        if (info) return installPublicFirmware(info);
        failFirmwareInstall("Could not start the firmware update.");
      });
  }

  function checkFirmwareUpdate(installAfterCheck) {
    if (S.firmware_checking || S.firmware_installing) return;
    S.firmware_install_error = "";
    S.firmware_checking = true;
    refreshFirmwareUi();
    post(endpoints.firmware_check + "/press")
      .then(function () { return delayMs(4000); })
      .then(function () { return safeGet(endpoints.update); })
      .then(function (data) {
        var available = applyFirmwareUpdateResponse(data);
        S.firmware_checking = false;
        if (installAfterCheck && available) startFirmwareInstall();
        else refreshFirmwareUi();
      })
      .catch(function () {
        S.firmware_checking = false;
        failFirmwareInstall("Could not check for a firmware update.");
      });
    fetchPublicFirmwareVersions();
  }

  function refreshC6FirmwareState() {
    return Promise.all([
      safeGet(endpoints.c6_current_firmware),
      safeGet(endpoints.c6_available_firmware),
      safeGet(endpoints.c6_update_status)
    ]).then(function (responses) {
      if (responses[0]) S.c6_current_firmware = responses[0].value || responses[0].state || S.c6_current_firmware;
      if (responses[1]) S.c6_available_firmware = responses[1].value || responses[1].state || S.c6_available_firmware;
      if (responses[2]) S.c6_update_status = responses[2].value || responses[2].state || S.c6_update_status;
      refreshC6FirmwareUi();
    });
  }

  function handleFirmwareReconnect() {
    if (!S.firmware_restart_pending) return;
    S.firmware_restart_pending = false;
    S.firmware_installing = false;
    S.firmware_uploading = false;
    S.firmware_install_error = "";
    fetchDeviceSettingsState().then(function () {
      showBanner("Firmware update complete.", "success");
      if (!isEditingSetting()) renderSettings();
    }).catch(function () { refreshFirmwareUi(); });
  }

  function makeFirmwareCard() {
    var fwBody = el("div", "fw-body");
    var subpanels = el("div", "fw-subpanels");

    var updateBody = el("div");
    var currentRow = el("div", "fw-row");
    currentRow.appendChild(textLabel("Current version", ""));
    var currentValue = el("span", "fw-label");
    currentRow.appendChild(currentValue);
    updateBody.appendChild(currentRow);
    els.fwCurrentVersion = currentValue;
    var latestRow = el("div", "fw-row");
    latestRow.appendChild(textLabel("Available version", ""));
    var latestValue = el("span", "fw-label");
    latestRow.appendChild(latestValue);
    updateBody.appendChild(latestRow);
    els.fwLatestVersion = latestValue;
    var updateActions = el("div", "fw-actions");
    var updateButton = button("Check for Update", "btn btn-secondary btn-sm", function () {
      if (firmwareUpdateKnownAvailable()) {
        if (S.update_available) startFirmwareInstall();
        else checkFirmwareUpdate(true);
      } else {
        checkFirmwareUpdate(false);
      }
    });
    updateActions.appendChild(updateButton);
    updateBody.appendChild(updateActions);
    els.fwActionBtn = updateButton;
    var updateStatus = el("div", "fw-status");
    updateBody.appendChild(updateStatus);
    els.fwStatus = updateStatus;
    var updateBadge = makeDisclosureBadge("Update available", "Firmware update available");
    els.firmwareUpdatesBadge = updateBadge;
    subpanels.appendChild(makeInlineDisclosure("Firmware updates", updateBody, false, updateBadge));

    var autoBody = el("div");
    var autoBadge = makeDisclosureBadge("On", "Automatic firmware updates on");
    var autoToggle = toggleSettingRow({
      label: "Auto Update",
      value: !!S.auto_update,
      getValue: function () { return !!S.auto_update; },
      setValue: function (value) { S.auto_update = value; },
      onChange: function () {
        saveSetting("auto_update", S.auto_update).catch(function () { S.auto_update = !S.auto_update; });
        refreshFirmwareUi();
      }
    });
    autoBody.appendChild(autoToggle.field);
    els.fwAutoToggle = autoToggle.toggle;
    var frequencyField = field("Update Frequency");
    frequencyField.appendChild(selectFromOptions(productSettingOptions("update_frequency"), S.update_frequency, function (value) {
      S.update_frequency = value;
      saveSetting("update_frequency", value);
    }));
    autoBody.appendChild(frequencyField);
    els.fwFrequencyField = frequencyField;
    els.autoUpdateBadge = autoBadge;
    subpanels.appendChild(makeInlineDisclosure("Auto updates", autoBody, false, autoBadge));

    var wifiBody = el("div");
    var c6CurrentRow = el("div", "fw-row");
    c6CurrentRow.appendChild(textLabel("Current", ""));
    var c6CurrentValue = el("span", "fw-label");
    c6CurrentRow.appendChild(c6CurrentValue);
    wifiBody.appendChild(c6CurrentRow);
    els.c6FirmwareCurrent = c6CurrentValue;
    var c6LatestRow = el("div", "fw-row");
    c6LatestRow.appendChild(textLabel("Available", ""));
    var c6LatestValue = el("span", "fw-label");
    c6LatestRow.appendChild(c6LatestValue);
    wifiBody.appendChild(c6LatestRow);
    els.c6FirmwareLatest = c6LatestValue;
    var c6AutoToggle = toggleSettingRow({
      label: "Auto Update",
      value: !!S.c6_auto_update,
      getValue: function () { return !!S.c6_auto_update; },
      setValue: function (value) { S.c6_auto_update = value; },
      onChange: function () {
        saveSetting("c6_auto_update", S.c6_auto_update).catch(function () { S.c6_auto_update = !S.c6_auto_update; });
        refreshC6FirmwareUi();
      }
    });
    wifiBody.appendChild(c6AutoToggle.field);
    els.c6AutoToggle = c6AutoToggle.toggle;
    var c6Actions = el("div", "fw-actions");
    var c6Button = button("Check for Update", "btn btn-secondary btn-sm", function () {
      if (S.c6_firmware_checking || S.c6_firmware_installing) return;
      if (c6FirmwareUpdateKnownAvailable()) {
        S.c6_firmware_installing = true;
        refreshC6FirmwareUi();
        post(endpoints.c6_firmware_install + "/press")
          .then(function () { return delayMs(5000); })
          .then(refreshC6FirmwareState)
          .catch(function () {
            S.c6_update_status = "Could not install the WiFi firmware update.";
            showBanner(S.c6_update_status, "error");
          })
          .finally(function () { S.c6_firmware_installing = false; refreshC6FirmwareUi(); });
      } else {
        S.c6_firmware_checking = true;
        refreshC6FirmwareUi();
        post(endpoints.c6_firmware_check + "/press")
          .then(function () { return delayMs(4000); })
          .then(refreshC6FirmwareState)
          .catch(function () {
            S.c6_update_status = "Could not check WiFi firmware.";
            showBanner(S.c6_update_status, "error");
          })
          .finally(function () { S.c6_firmware_checking = false; refreshC6FirmwareUi(); });
      }
    });
    c6Actions.appendChild(c6Button);
    wifiBody.appendChild(c6Actions);
    els.c6FirmwareActionBtn = c6Button;
    var c6Status = el("div", "fw-status");
    wifiBody.appendChild(c6Status);
    els.c6FirmwareStatus = c6Status;
    var c6Badge = makeDisclosureBadge("Update available", "WiFi firmware update available");
    els.c6FirmwareBadge = c6Badge;
    subpanels.appendChild(makeInlineDisclosure("WiFi firmware", wifiBody, false, c6Badge));

    var previousBody = el("div");
    var versionField = field("Version");
    var versionSelect = document.createElement("select");
    versionSelect.onchange = function () { S.firmware_selected_version = versionSelect.value; refreshPreviousFirmwareUi(); };
    versionField.appendChild(versionSelect);
    previousBody.appendChild(versionField);
    els.fwVersionSelect = versionSelect;
    var previousActions = el("div", "fw-previous-actions");
    var previousInstall = button("Install", "btn btn-secondary btn-sm", function () {
      var info = selectedPreviousFirmwareInfo();
      if (!info) return;
      if (!window.confirm("Install older firmware " + info.version + "? The display will restart during installation.")) return;
      installPublicFirmware(info);
    });
    previousActions.appendChild(previousInstall);
    previousBody.appendChild(previousActions);
    els.fwPreviousInstallBtn = previousInstall;
    var previousPanel = makeInlineDisclosure("Previous firmware", previousBody, false);
    els.fwPreviousPanel = previousPanel;
    subpanels.appendChild(previousPanel);

    fwBody.appendChild(subpanels);
    var cardBadge = makeBadge(false, "Update available", "Firmware update available");
    els.firmwareCardBadge = cardBadge;
    var firmwareCard = makeCollapsibleCard("Firmware", fwBody, true, cardBadge);
    refreshFirmwareUi();
    refreshC6FirmwareUi();
    fetchPublicFirmwareVersions();
    return firmwareCard;
  }

  function makeDeviceRebootCard() {
    var rebootBody = el("div", "fw-body");
    var rebootLabel = textLabel("", "Device Reboot");
    var rebootBtn = button("Reboot Screen", "btn btn-secondary btn-sm", function () {
      rebootBtn.disabled = true;
      rebootBtn.textContent = "Rebooting...";
      post(endpoints.reboot_screen + "/press")
        .catch(function () {
          // Shared request helpers already surface failures in the UI.
        })
        .finally(function () {
          setTimeout(function () {
            rebootBtn.disabled = false;
            rebootBtn.textContent = "Reboot Screen";
          }, 3000);
        });
    });
    rebootBody.appendChild(actionRow(rebootLabel, rebootBtn));
    return makeCollapsibleCard("Device Reboot", rebootBody, true);
  }

  function makeDeveloperCard() {
    if (!developerPanelEnabledByUrl()) return null;
    var devBadge = makeBadge(S.developer_features_enabled);
    var devBody = el("div");
    devBody.appendChild(toggleSettingRow({
      label: "Enable in-development features",
      value: S.developer_features_enabled,
      getValue: function () { return S.developer_features_enabled; },
      setValue: function (value) { S.developer_features_enabled = value; },
      badge: devBadge,
      onChange: function () {
        saveSetting("developer_features_enabled", S.developer_features_enabled);
        if (!S.developer_features_enabled && isPortraitScreenRotation(S.screen_rotation)) {
          saveSetting("screen_rotation", "0");
        }
        renderSettings();
      }
    }).field);
    return makeCollapsibleCard("Developer", devBody, true, devBadge);
  }
