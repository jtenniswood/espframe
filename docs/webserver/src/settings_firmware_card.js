  function makeFirmwareCard() {
    // Firmware
    var fwBody = el("div", "fw-body");
    var versionRow = el("div", "field fw-row");
    var versionLabel = el("span", "fw-label");
    versionLabel.innerHTML = '<span style="color:var(--text2)">Installed</span> ' +
      esc(displayVersion(S.firmware || S.installed_version, "Dev"));
    var checkBtn = el("button", "btn btn-secondary btn-sm");
    checkBtn.textContent = "Check for Update";
    var statusMsg = el("span", "fw-status");
    versionRow.appendChild(versionLabel);
    var checkWrap = el("div");
    checkWrap.className = "check-wrap";
    checkWrap.appendChild(statusMsg);
    checkWrap.appendChild(checkBtn);
    versionRow.appendChild(checkWrap);
    var versionBlock = el("div");
    versionBlock.appendChild(versionRow);
    fwBody.appendChild(versionBlock);

    var updatesSection = el("div", "fw-updates");
    var updateRow = el("div");
    updatesSection.appendChild(updateRow);
    var betaRow = el("div");
    updatesSection.appendChild(betaRow);
    fwBody.appendChild(updatesSection);

    var rebootRow = el("div", "field fw-row");
    var rebootLabel = el("span", "fw-label");
    rebootLabel.textContent = "Device Reboot";
    var rebootBtn = el("button", "btn btn-secondary btn-sm");
    rebootBtn.textContent = "Reboot Screen";
    rebootBtn.onclick = function () {
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
    };
    rebootRow.appendChild(rebootLabel);
    rebootRow.appendChild(rebootBtn);
    fwBody.appendChild(rebootRow);

    function renderUpdateRow() {
      updateRow.innerHTML = "";
      if (!S.update_available) return;
      var row = el("div", "field fw-row");
      var label = el("span", "fw-label");
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
      if (!S.beta_available) return;
      var row = el("div", "field fw-row");
      var label = el("span", "fw-label");
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
      post(endpoints.firmware_check + "/press")
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
          if (!S.beta_channel) {
            S.beta_available = false;
            S.beta_version = "";
            renderBetaRow();
            return null;
          }
          return safeGet(endpoints.update_beta);
        })
        .then(function (betaData) {
          if (betaData && (betaData.latest_version || betaData.value)) {
            S.beta_version = betaData.latest_version || betaData.value;
            S.beta_available = betaData.current_version
              ? betaData.latest_version !== betaData.current_version
              : betaData.state === "UPDATE AVAILABLE";
          }
          renderBetaRow();
          if (!S.update_available && !S.beta_available) {
            statusMsg.textContent = "Up to date";
            statusMsg.style.color = "var(--success)";
          }
        });
    };

    var autoUpdateOptions = ["Disabled"].concat(productSettingOptions("update_frequency"));
    var currentAutoUpdate = S.auto_update ? S.update_frequency : "Disabled";
    var freqField = field("Auto updates");
    freqField.appendChild(
      selectFromOptions(autoUpdateOptions, currentAutoUpdate, function (v) {
        if (v === "Disabled") {
          saveSetting("auto_update", false);
        } else {
          saveSetting("auto_update", true);
          saveSetting("update_frequency", v);
        }
      })
    );
    fwBody.appendChild(freqField);

    var betaChannelField = field("");
    var betaChannelRow = el("div", "toggle-row");
    betaChannelRow.innerHTML = "<span>Beta Channel</span>";
    var betaChannelToggle = el("div", S.beta_channel ? "toggle on" : "toggle");
    betaChannelToggle.onclick = function () {
      S.beta_channel = !S.beta_channel;
      betaChannelToggle.className = S.beta_channel ? "toggle on" : "toggle";
      saveSetting("beta_channel", S.beta_channel);
      if (!S.beta_channel) {
        S.beta_available = false;
        S.beta_version = "";
        renderBetaRow();
      }
    };
    betaChannelRow.appendChild(betaChannelToggle);
    betaChannelField.appendChild(betaChannelRow);
    fwBody.appendChild(betaChannelField);

    var firmwareUrlStatus = el("div", "status");
    function setFirmwareUrlStatus(msg, ok) {
      firmwareUrlStatus.innerHTML = '<span class="dot ' + (ok ? "green" : "red") + '"></span> ' + msg;
      clearTimeout(firmwareUrlStatus._t);
      if (ok) {
        firmwareUrlStatus._t = setTimeout(function () {
          firmwareUrlStatus.textContent = "";
        }, 3000);
      }
    }

    function makeFirmwareUrlField(label, key, placeholder) {
      var f = field(label);
      var firmwareUrlInput = input("url", S[key], placeholder, MAX_FIRMWARE_URL_LENGTH);
      var firmwareUrlError = el("div", "field-error");
      firmwareUrlInput.onchange = function () {
        var url = normalizeFirmwareManifestUrl(firmwareUrlInput.value);
        firmwareUrlError.textContent = "";
        if (url && !isValidHttpUrl(url)) {
          firmwareUrlError.textContent = "Use a full http:// or https:// URL";
          return;
        }
        saveSetting(key, url)
          .then(function (r) {
            if (!r || !r.ok) throw new Error("save_failed");
            return delayMs(500);
          })
          .then(function () {
            return safeGet(endpoints[key]);
          })
          .then(function (resp) {
            var saved = normalizeFirmwareManifestUrl((resp && (resp.value || resp.state)) || url);
            S[key] = saved;
            firmwareUrlInput.value = saved;
            setFirmwareUrlStatus("Update URL saved", true);
          })
          .catch(function () {
            setFirmwareUrlStatus("Failed to save update URL", false);
          });
      };
      f.appendChild(firmwareUrlInput);
      f.appendChild(firmwareUrlError);
      return f;
    }

    var firmwareUrlsHint = el("div", "field-hint");
    firmwareUrlsHint.textContent = "Advanced: use a custom manifest to check and install firmware from another location.";
    fwBody.appendChild(firmwareUrlsHint);
    fwBody.appendChild(makeFirmwareUrlField(
      "Stable Manifest URL",
      "firmware_manifest_url",
      FIRMWARE_MANIFEST_URLS.stable
    ));
    fwBody.appendChild(makeFirmwareUrlField(
      "Beta Manifest URL",
      "firmware_beta_manifest_url",
      FIRMWARE_MANIFEST_URLS.beta
    ));
    fwBody.appendChild(firmwareUrlStatus);

    return makeCollapsibleCard("Firmware", fwBody, true);
  }

  function makeWifiCard() {
    var wifiBody = el("div", "fw-body");

    var currentRow = el("div", "field fw-row");
    var currentLabel = el("span", "fw-label");
    currentLabel.innerHTML = '<span style="color:var(--text2)">Current C6 firmware</span> ' +
      esc(displayVersion(S.c6_current_firmware, "Unknown"));
    currentRow.appendChild(currentLabel);
    wifiBody.appendChild(currentRow);

    var availableRow = el("div", "field fw-row");
    var availableLabel = el("span", "fw-label");
    availableLabel.innerHTML = '<span style="color:var(--text2)">Available firmware</span> ' +
      esc(displayVersion(S.c6_available_firmware, "Unknown"));
    var installBtn = el("button", "btn btn-primary btn-sm");
    installBtn.textContent = "Install";
    installBtn.onclick = function () {
      installBtn.disabled = true;
      installBtn.textContent = "Installing\u2026";
      post(endpoints.c6_firmware_install + "/press")
        .catch(function () {
          // Shared request helpers already surface failures in the UI.
        })
        .finally(function () {
          setTimeout(function () {
            installBtn.disabled = false;
            installBtn.textContent = "Install";
          }, 3000);
        });
    };
    availableRow.appendChild(availableLabel);
    availableRow.appendChild(installBtn);
    wifiBody.appendChild(availableRow);

    return makeCollapsibleCard("WiFi", wifiBody, true);
  }

  function makeDeveloperCard() {
    if (!developerPanelEnabledByUrl()) return null;
    var devBadge = makeBadge(S.developer_features_enabled);
    var devBody = el("div");
    var devField = field("");
    var devRow = el("div", "toggle-row");
    devRow.innerHTML = "<span>Enable in-development features</span>";
    var devToggle = el("div", S.developer_features_enabled ? "toggle on" : "toggle");
    devToggle.onclick = function () {
      S.developer_features_enabled = !S.developer_features_enabled;
      devToggle.className = S.developer_features_enabled ? "toggle on" : "toggle";
      devBadge.className = "on-badge" + (S.developer_features_enabled ? " active" : "");
      saveSetting("developer_features_enabled", S.developer_features_enabled);
      if (!S.developer_features_enabled && isPortraitScreenRotation(S.screen_rotation)) {
        saveSetting("screen_rotation", "0");
      }
      renderSettings();
    };
    devRow.appendChild(devToggle);
    devField.appendChild(devRow);
    devBody.appendChild(devField);
    return makeCollapsibleCard("Developer", devBody, true, devBadge);
  }
