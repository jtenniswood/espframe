  function makeFirmwareCard() {
    // Firmware
    var fwBody = el("div", "fw-body");
    var versionLabel = textLabel("Installed", displayVersion(S.firmware || S.installed_version, "Dev"));
    var checkBtn = button("Check for Update", "btn btn-secondary btn-sm");
    var statusMsg = el("span", "fw-status");
    var checkWrap = el("div");
    checkWrap.className = "check-wrap";
    checkWrap.appendChild(statusMsg);
    checkWrap.appendChild(checkBtn);
    var versionBlock = el("div");
    versionBlock.appendChild(actionRow(versionLabel, checkWrap));
    fwBody.appendChild(versionBlock);

    var updatesSection = el("div", "fw-updates");
    var updateRow = el("div");
    updatesSection.appendChild(updateRow);
    fwBody.appendChild(updatesSection);

    function renderUpdateRow() {
      updateRow.replaceChildren();
      if (!S.update_available) return;
      var label = textLabel("Stable", S.latest_version);
      var installBtn = button("Install", "btn btn-primary btn-sm", function () {
        installBtn.disabled = true;
        installBtn.textContent = "Installing\u2026";
        post(endpoints.update + "/install");
      });
      updateRow.appendChild(actionRow(label, installBtn));
    }

    renderUpdateRow();

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
          var hasUpdate = data && data.value &&
            (data.current_version
              ? data.current_version !== data.latest_version
              : data.state === "UPDATE AVAILABLE");
          if (hasUpdate) {
            S.update_available = true;
            S.latest_version = data.latest_version || data.value;
            renderUpdateRow();
          }
          if (!S.update_available) {
            statusMsg.textContent = "Up to date";
            statusMsg.style.color = "var(--success)";
          }
        })
        .catch(function () {
          // Shared request helpers already surface failures in the UI.
        })
        .finally(function () {
          checkBtn.disabled = false;
          checkBtn.textContent = "Check for Update";
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

    var firmwareUrlStatus = el("div", "status");
    function setFirmwareUrlStatus(msg, ok) {
      setStatus(firmwareUrlStatus, msg, ok ? "green" : "red", ok ? 3000 : null);
    }

    function makeFirmwareUrlField(label, key, placeholder) {
      var f = field(label);
      var firmwareUrlInput = input("url", S[key], placeholder, productTextMaxLength(key, MAX_FIRMWARE_URL_LENGTH));
      var firmwareUrlError = makeFieldError();
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

    var advancedBody = el("div", "fw-advanced-body");
    var firmwareUrlsHint = el("div", "field-hint");
    firmwareUrlsHint.textContent = "Use a custom manifest to check and install firmware from another location.";
    advancedBody.appendChild(firmwareUrlsHint);
    advancedBody.appendChild(makeFirmwareUrlField(
      "Stable Manifest URL",
      "firmware_manifest_url",
      FIRMWARE_MANIFEST_URLS.stable
    ));
    advancedBody.appendChild(firmwareUrlStatus);

    var advancedPanel = document.createElement("details");
    advancedPanel.className = "inline-expander";
    var advancedSummary = document.createElement("summary");
    advancedSummary.textContent = "Advanced";
    advancedPanel.appendChild(advancedSummary);
    advancedPanel.appendChild(advancedBody);
    fwBody.appendChild(advancedPanel);

    return makeCollapsibleCard("Firmware", fwBody, true);
  }

  function makeWifiCard() {
    var wifiBody = el("div", "fw-body");

    wifiBody.appendChild(actionRow(
      textLabel("Current C6 firmware", displayVersion(S.c6_current_firmware, "Unknown")),
      document.createElement("span")
    ));

    var availableLabel = textLabel("Available firmware", displayVersion(S.c6_available_firmware, "Unknown"));
    var actionWrap = el("div");
    actionWrap.className = "check-wrap";
    var checkBtn = button("Check", "btn btn-secondary btn-sm", function () {
      checkBtn.disabled = true;
      checkBtn.textContent = "Checking...";
      post(endpoints.c6_firmware_check + "/press")
        .catch(function () {
          // Shared request helpers already surface failures in the UI.
        })
        .finally(function () {
          setTimeout(function () {
            checkBtn.disabled = false;
            checkBtn.textContent = "Check";
          }, 3000);
        });
    });
    var installBtn = button("Install", "btn btn-primary btn-sm", function () {
      installBtn.disabled = true;
      installBtn.textContent = "Installing...";
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
    });
    actionWrap.appendChild(checkBtn);
    actionWrap.appendChild(installBtn);
    wifiBody.appendChild(actionRow(availableLabel, actionWrap));

    return makeCollapsibleCard("WiFi", wifiBody, true);
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
