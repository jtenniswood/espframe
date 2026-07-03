  // --- Import / Export ---

  function backupExportFieldValue(entry) {
    if (!entry || !Array.isArray(entry.state_keys) || !entry.state_keys.length) return "";
    if (entry.group === "screen" && entry.field === "schedule_wake_timeout") {
      return normalizeScheduleWakeTimeout(S.schedule_wake_timeout);
    }
    if (entry.state_keys.length > 1) {
      return entry.state_keys.map(function (key) {
        return S[key];
      });
    }
    return S[entry.state_keys[0]];
  }

  function buildBackupExportData() {
    var data = {
      version: BACKUP_CONFIG_VERSION,
      exported_at: new Date().toISOString()
    };
    BACKUP_SCHEMA.forEach(function (entry) {
      if (!entry || !entry.group || !entry.field) return;
      if (!data[entry.group]) data[entry.group] = {};
      data[entry.group][entry.field] = backupExportFieldValue(entry);
    });
    return data;
  }

  var BACKUP_VERSION_MIGRATIONS = {
    1: function backupConfigVersion1(data) {
      return data;
    }
  };

  function validateBackupConfigVersion(data) {
    if (!data || typeof data !== "object" || !Object.prototype.hasOwnProperty.call(data, "version")) {
      return "Invalid config file - missing version";
    }
    if (typeof data.version !== "number" || !isFinite(data.version) || Math.floor(data.version) !== data.version) {
      return "Unsupported backup version " + String(data.version);
    }
    if (data.version > BACKUP_CONFIG_VERSION) {
      return "Unsupported backup version " + data.version + " - this device supports version " + BACKUP_CONFIG_VERSION;
    }
    if (!BACKUP_VERSION_MIGRATIONS[data.version]) {
      return "Unsupported backup version " + data.version;
    }
    return "";
  }

  function migrateBackupConfig(data) {
    return BACKUP_VERSION_MIGRATIONS[data.version](data);
  }

  function exportConfig() {
    var data = buildBackupExportData();
    var json = JSON.stringify(data, null, 2);
    var blob = new Blob([json], { type: "application/json" });
    var url = URL.createObjectURL(blob);
    var now = new Date();
    var name = "espframe-config-" +
      now.getFullYear() + "-" +
      String(now.getMonth() + 1).padStart(2, "0") + "-" +
      String(now.getDate()).padStart(2, "0") + ".json";
    var a = document.createElement("a");
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }

  function backupEntryKey(entry) {
    return entry.group + "." + entry.field;
  }

  function backupImportFieldPresent(data, entry) {
    var groupData = data[entry.group] || {};
    return groupData[entry.field] !== undefined;
  }

  function backupImportFieldValue(data, entry) {
    return (data[entry.group] || {})[entry.field];
  }

  function backupImportStateKey(entry) {
    return entry && Array.isArray(entry.state_keys) && entry.state_keys.length ? entry.state_keys[0] : "";
  }

  var backupImportSavePromises = null;

  function trackBackupImportSave(result) {
    if (backupImportSavePromises) backupImportSavePromises.push(Promise.resolve(result));
  }

  function backupImportEntryUsesPhotoSourceApply(entry) {
    return entry && entry.group === "photos" && Array.isArray(entry.state_keys) &&
      entry.state_keys.some(settingUsesPhotoSourceApply);
  }

  function applyGenericBackupImportField(entry, value) {
    var stateKey = backupImportStateKey(entry);
    if (!stateKey || !endpoints[stateKey]) return false;
    trackBackupImportSave(saveSetting(stateKey, value));
    return true;
  }

  function skipBackupImportField(message) {
    showBanner(message, "error");
    return false;
  }

  function backupImportSummaryMessage(appliedCount, skippedCount) {
    if (!skippedCount) return "Settings imported successfully";
    var skippedText = skippedCount + " skipped " + (skippedCount === 1 ? "setting" : "settings");
    if (appliedCount) return "Imported with " + skippedText;
    return "Import skipped " + skippedCount + " " + (skippedCount === 1 ? "setting" : "settings");
  }

  function applyBackupImportField(entry, value) {
    switch (backupEntryKey(entry)) {
      case "connection.immich_url":
        trackBackupImportSave(saveSetting("immich_url", normalizeImmichUrl(value)));
        return true;
      case "connection.api_key":
        trackBackupImportSave(saveSetting("api_key", value));
        return true;
      case "photos.album_ids":
        var importAlbum = String(value).trim();
        if (photoIdFieldTooLong(importAlbum)) {
          return skipBackupImportField("Album IDs exceed 255 characters - not imported");
        } else if (!isValidUuidList(importAlbum)) {
          return skipBackupImportField("Import skipped invalid album IDs");
        } else {
          trackBackupImportSave(saveSetting("album_ids", importAlbum));
        }
        return true;
      case "photos.album_labels":
        var importAlbumLabels = String(value).trim();
        if (photoLabelFieldTooLong(importAlbumLabels)) {
          return skipBackupImportField("Album labels exceed 255 characters - not imported");
        } else {
          trackBackupImportSave(saveSetting("album_labels", importAlbumLabels));
        }
        return true;
      case "photos.person_ids":
        var importPerson = String(value).trim();
        if (photoIdFieldTooLong(importPerson)) {
          return skipBackupImportField("Person IDs exceed 255 characters - not imported");
        } else if (!isValidUuidList(importPerson)) {
          return skipBackupImportField("Import skipped invalid person IDs");
        } else {
          trackBackupImportSave(saveSetting("person_ids", importPerson));
        }
        return true;
      case "photos.person_labels":
        var importPersonLabels = String(value).trim();
        if (photoLabelFieldTooLong(importPersonLabels)) {
          return skipBackupImportField("Person labels exceed 255 characters - not imported");
        } else {
          trackBackupImportSave(saveSetting("person_labels", importPersonLabels));
        }
        return true;
      case "photos.tag_ids":
        var importTag = String(value).trim();
        if (photoIdFieldTooLong(importTag)) {
          return skipBackupImportField("Tag IDs exceed 255 characters - not imported");
        } else if (!isValidUuidList(importTag)) {
          return skipBackupImportField("Import skipped invalid tag IDs");
        } else {
          trackBackupImportSave(saveSetting("tag_ids", importTag));
        }
        return true;
      case "photos.tag_labels":
        var importTagLabels = String(value).trim();
        if (photoLabelFieldTooLong(importTagLabels)) {
          return skipBackupImportField("Tag labels exceed 255 characters - not imported");
        } else {
          trackBackupImportSave(saveSetting("tag_labels", importTagLabels));
        }
        return true;
      case "firmware_updates.manifest_url":
        var importManifestUrl = normalizeFirmwareManifestUrl(value);
        if (importManifestUrl && !isValidHttpUrl(importManifestUrl)) {
          return skipBackupImportField("Stable firmware URL was invalid - not imported");
        } else {
          trackBackupImportSave(saveSetting("firmware_manifest_url", importManifestUrl));
        }
        return true;
      case "clock.timezone":
        trackBackupImportSave(saveSetting("timezone", normalizeTimezoneOption(value)));
        return true;
      case "clock.ntp_servers":
        if (Array.isArray(value)) {
          ["ntp_server_1", "ntp_server_2", "ntp_server_3"].forEach(function (key, idx) {
            if (value[idx] === undefined) return;
            trackBackupImportSave(saveSetting(key, value[idx]));
          });
          return true;
        }
        return skipBackupImportField("NTP servers were invalid - not imported");
      case "screen.schedule_wake_timeout":
        trackBackupImportSave(saveSetting("schedule_wake_timeout", value));
        return true;
      case "screen.rotation":
        var importedRotation = String(value);
        if (screenRotationOptionsForUi().indexOf(importedRotation) !== -1) {
          trackBackupImportSave(saveSetting("screen_rotation", importedRotation));
          return true;
        }
        return skipBackupImportField("Screen rotation was invalid - not imported");
      default:
        return applyGenericBackupImportField(entry, value);
    }
  }

  function importConfig() {
    var fileInput = document.createElement("input");
    fileInput.type = "file";
    fileInput.accept = ".json";
    fileInput.style.display = "none";

    fileInput.addEventListener("change", function () {
      if (!fileInput.files || !fileInput.files[0]) return;
      var reader = new FileReader();
      reader.onload = function () {
        var data;
        try { data = JSON.parse(reader.result); } catch (_) {
          showBanner("Invalid file \u2014 could not parse JSON", "error");
          return;
        }

        var versionError = validateBackupConfigVersion(data);
        if (versionError) {
          showBanner(versionError, "error");
          return;
        }
        data = migrateBackupConfig(data);

        backupImportSavePromises = [];
        var appliedCount = 0;
        var skippedCount = 0;
        var needsPhotoSourceApply = false;
        BACKUP_SCHEMA.forEach(function (entry) {
          if (!backupImportFieldPresent(data, entry)) return;
          if (applyBackupImportField(entry, backupImportFieldValue(data, entry))) {
            appliedCount += 1;
            needsPhotoSourceApply = needsPhotoSourceApply || backupImportEntryUsesPhotoSourceApply(entry);
          } else {
            skippedCount += 1;
          }
        });

        Promise.all(backupImportSavePromises)
          .then(function () {
            if (needsPhotoSourceApply) return post(endpoints.apply_photo_source + "/press");
            return null;
          })
          .then(function () {
            showBanner(backupImportSummaryMessage(appliedCount, skippedCount), skippedCount ? "error" : "success");
            renderSettings();
            backupImportSavePromises = null;
          });
      };
      reader.readAsText(fileInput.files[0]);
    });

    document.body.appendChild(fileInput);
    fileInput.click();
    document.body.removeChild(fileInput);
  }
