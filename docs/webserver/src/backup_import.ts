  // --- Import / Export ---

  function backupExportFieldValue(entry) {
    if (!entry || !Array.isArray(entry.state_keys) || !entry.state_keys.length) return "";
    if (entry.field === "api_key") return "";
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
    if (frameIdentity) data["identity"] = { name: frameIdentity.name };
    return data;
  }

  var BACKUP_VERSION_MIGRATIONS = {
    1: function backupConfigVersion1(data) {
      var migrated = JSON.parse(JSON.stringify(data));
      var photos = migrated.photos;
      if (!photos || !Object.prototype.hasOwnProperty.call(photos, "source")) {
        migrated.version = 2;
        return migrated;
      }
      var source = photos.source || "All Photos";
      photos.albums_enabled = source === "Album";
      photos.people_enabled = source === "Person";
      photos.tags_enabled = source === "Tag";
      photos.favorites_enabled = source === "Favorites";
      photos.rating_enabled = false;
      photos.location_enabled = false;
      photos.inclusion_matching = "Match all enabled groups";
      photos.album_matching = "Any selected album";
      photos.person_matching = "Any selected person";
      photos.favorite_mode = source === "Favorites" ? "Favorites only" : "Any";
      photos.minimum_rating = "Any";
      photos.country = "";
      photos.state = "";
      photos.city = "";
      photos.excluded_album_ids = "";
      photos.excluded_album_labels = "";
      photos.excluded_person_ids = "";
      photos.excluded_person_labels = "";
      photos.excluded_tag_ids = "";
      photos.excluded_tag_labels = "";
      // Keep the deprecated value for the one import write so firmware can
      // raise the one-time migration notice before converting it to All Photos.
      photos.source = source;
      migrated.version = 3;
      return migrated;
    },
    2: function backupConfigVersion2(data) {
      var migrated = JSON.parse(JSON.stringify(data));
      var photos = migrated.photos;
      if (photos) {
        [["albums_enabled", "excluded_album_ids"], ["people_enabled", "excluded_person_ids"],
          ["tags_enabled", "excluded_tag_ids"]].forEach(function (group) {
          if (String(photos[group[1]] || "").trim()) photos[group[0]] = true;
        });
        if (!Object.prototype.hasOwnProperty.call(photos, "favorites_enabled")) {
          photos.favorites_enabled = photos.favorite_mode && photos.favorite_mode !== "Any";
        }
        if (!Object.prototype.hasOwnProperty.call(photos, "rating_enabled")) {
          photos.rating_enabled = photos.minimum_rating && photos.minimum_rating !== "Any";
        }
        if (!Object.prototype.hasOwnProperty.call(photos, "location_enabled")) {
          photos.location_enabled = !!(String(photos.country || "").trim() ||
            String(photos.state || "").trim() || String(photos.city || "").trim());
        }
      }
      migrated.version = 3;
      return migrated;
    },
    3: function backupConfigVersion3(data) {
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
    if (!frameIdentityLoaded) return;
    var data = buildBackupExportData();
    var json = JSON.stringify(data, null, 2);
    var blob = new Blob([json], { type: "application/json" });
    var url = URL.createObjectURL(blob);
    var now = new Date();
    var name = "espframe-config-" +
      now.getFullYear() + "-" +
      String(now.getMonth() + 1).padStart(2, "0") + "-" +
      String(now.getDate()).padStart(2, "0") + ".json";
    if (frameIdentity && frameIdentity.name) name = name.replace("espframe", frameIdentity.hostname);
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

  var backupImportSaveTasks = null;
  var backupImportInProgress = false;
  var backupImportMessages: string[] = [];
  var backupImportValues = null;
  var BACKUP_IMPORT_MAX_BODY = 900;

  function queueBackupImportSetting(key, value) {
    if (!backupImportValues) return false;
    backupImportValues[key] = value;
    return true;
  }

  function recordBackupImportSaveFailure(label) {
    if (backupImportMessages.length >= 3) return;
    backupImportMessages.push("Could not restore " + label);
  }

  function trackBackupImportSave(result, settingCount?) {
    if (!backupImportSaveTasks) return;
    var tracked = Promise.resolve(result)
      .then(function (response) {
        if (response && response.ok === false) throw new Error("save_failed");
        if (response && typeof response.failedCount === "number") return response;
        return { failedCount: 0 };
      })
      .catch(function () {
        return { failedCount: Math.max(1, Number(settingCount) || 1) };
      });
    backupImportSaveTasks.push(tracked);
  }

  function backupImportBatchBodyLength(values) {
    var configuration = JSON.stringify({ api_version: 1, reset_epoch: 0, values: values });
    return new URLSearchParams({ configuration: configuration }).toString().length;
  }

  function backupImportSettingsBatches(values) {
    var batches = [];
    var current = {};
    Object.keys(values).forEach(function (key) {
      var candidate = Object.assign({}, current);
      candidate[key] = values[key];
      if (Object.keys(current).length && backupImportBatchBodyLength(candidate) > BACKUP_IMPORT_MAX_BODY) {
        batches.push(current);
        current = {};
        candidate = {};
        candidate[key] = values[key];
      }
      current = candidate;
    });
    if (Object.keys(current).length) batches.push(current);
    return batches;
  }

  function backupImportCanRetrySmaller(error) {
    return error && (error.status === 413 || error.status === 422 ||
      (error.status === 400 && (!error.code || error.code === "invalid_json")));
  }

  function saveBackupImportBatch(values) {
    var keys = Object.keys(values);
    return saveSettingValues(values).then(function () {
      return { failedCount: 0 };
    }).catch(function (error) {
      if (keys.length > 1 && backupImportCanRetrySmaller(error)) {
        var midpoint = Math.ceil(keys.length / 2);
        var first = {};
        var second = {};
        keys.forEach(function (key, index) {
          (index < midpoint ? first : second)[key] = values[key];
        });
        return saveBackupImportBatch(first).then(function (firstResult) {
          return saveBackupImportBatch(second).then(function (secondResult) {
            return { failedCount: firstResult.failedCount + secondResult.failedCount };
          });
        });
      }
      recordBackupImportSaveFailure(keys.length === 1 ? keys[0].replace(/_/g, " ") : "settings");
      return { failedCount: keys.length };
    });
  }

  function saveBackupImportSettings(values) {
    var batches = backupImportSettingsBatches(values);
    return batches.reduce(function (chain, batch) {
      return chain.then(function (result) {
        return saveBackupImportBatch(batch).then(function (batchResult) {
          return { failedCount: result.failedCount + batchResult.failedCount };
        });
      });
    }, Promise.resolve({ failedCount: 0 }));
  }

  function backupImportEntryUsesPhotoSourceApply(entry) {
    return entry && entry.group === "photos" && Array.isArray(entry.state_keys) &&
      entry.state_keys.some(settingUsesPhotoSourceApply);
  }

  function backupImportSettingSpec(entry) {
    var stateKey = backupImportStateKey(entry);
    return stateKey && PRODUCT_SETTINGS ? PRODUCT_SETTINGS[stateKey] : null;
  }

  function backupImportFieldLabel(entry) {
    return backupEntryKey(entry).replace(/_/g, " ");
  }

  function backupImportValidation(ok, value, message?) {
    return { ok: ok, value: value, message: message || "" };
  }

  function isValidBackupDate(value) {
    if (value === "") return true;
    if (!/^\d{4}-\d{2}-\d{2}$/.test(value)) return false;
    var parts = value.split("-").map(Number);
    var date = new Date(Date.UTC(parts[0], parts[1] - 1, parts[2]));
    return date.getUTCFullYear() === parts[0] &&
      date.getUTCMonth() === parts[1] - 1 &&
      date.getUTCDate() === parts[2];
  }

  function backupNumberStepAligned(value, minimum, step) {
    if (!step || step <= 0) return true;
    var offset = (value - minimum) / step;
    return Math.abs(offset - Math.round(offset)) < 1e-9;
  }

  function validateProductSettingBackupImport(entry, value) {
    var stateKey = backupImportStateKey(entry);
    var spec = backupImportSettingSpec(entry);
    if (!stateKey || !endpoints[stateKey]) {
      return backupImportValidation(false, value, backupImportFieldLabel(entry) + " is not supported");
    }
    if (!spec || !spec.domain) return backupImportValidation(true, value);

    if (spec.domain === "switch") {
      if (typeof value === "boolean") return backupImportValidation(true, value);
      if (typeof value === "string") {
        var normalizedSwitch = value.trim().toLowerCase();
        if (normalizedSwitch === "true" || normalizedSwitch === "on") return backupImportValidation(true, true);
        if (normalizedSwitch === "false" || normalizedSwitch === "off") return backupImportValidation(true, false);
      }
      return backupImportValidation(false, value, backupImportFieldLabel(entry) + " was invalid - not imported");
    }

    if (spec.domain === "number") {
      var numberValue = Number(value);
      if (!isFinite(numberValue)) {
        return backupImportValidation(false, value, backupImportFieldLabel(entry) + " was not a number - not imported");
      }
      var minimum = spec.min !== undefined ? Number(spec.min) : -Infinity;
      var maximum = spec.max !== undefined ? Number(spec.max) : Infinity;
      var step = spec.step !== undefined ? Number(spec.step) : 0;
      if (numberValue < minimum || numberValue > maximum || !backupNumberStepAligned(numberValue, minimum, step)) {
        return backupImportValidation(false, value, backupImportFieldLabel(entry) + " was outside the device range - not imported");
      }
      return backupImportValidation(true, numberValue);
    }

    if (spec.domain === "select") {
      var optionValue = String(value);
      if (productSettingOptions(stateKey, true).indexOf(optionValue) === -1) {
        return backupImportValidation(false, value, backupImportFieldLabel(entry) + " had an unsupported option - not imported");
      }
      return backupImportValidation(true, optionValue);
    }

    if (spec.domain === "text") {
      var textValue = value == null ? "" : String(value).trim();
      if (spec.maxLength !== undefined && textValue.length > Number(spec.maxLength)) {
        return backupImportValidation(false, value, backupImportFieldLabel(entry) + " exceeded the device length limit - not imported");
      }
      if ((stateKey === "date_from" || stateKey === "date_to") && !isValidBackupDate(textValue)) {
        return backupImportValidation(false, value, backupImportFieldLabel(entry) + " was not a valid date - not imported");
      }
      return backupImportValidation(true, textValue);
    }

    return backupImportValidation(true, value);
  }

  function applyGenericBackupImportField(entry, value) {
    var validation = validateProductSettingBackupImport(entry, value);
    if (!validation.ok) return skipBackupImportField(validation.message);
    return queueBackupImportSetting(backupImportStateKey(entry), validation.value);
  }

  function skipBackupImportField(message) {
    if (backupImportInProgress) {
      if (backupImportMessages.length < 3) backupImportMessages.push(message);
      return false;
    }
    showBanner(message, "error");
    return false;
  }

  function backupImportSummaryMessage(appliedCount, skippedCount, failedCount) {
    var skippedText = skippedCount + " skipped " + (skippedCount === 1 ? "setting" : "settings");
    var failedText = failedCount + " failed " + (failedCount === 1 ? "setting" : "settings");
    if (failedCount) {
      if (appliedCount || skippedCount) {
        return "Backup partially restored: " + failedText + (skippedCount ? " and " + skippedText : "");
      }
      return "Backup restore failed for " + failedCount + " " + (failedCount === 1 ? "setting" : "settings");
    }
    if (!skippedCount) return "Backup restored successfully";
    return "Backup restored successfully; " + skippedText;
  }

  function applyBackupImportField(entry, value) {
    var entryKey = backupEntryKey(entry);
    if (/^photos\.excluded_(album|person|tag)_ids$/.test(entryKey)) {
      var excludedIds = String(value == null ? "" : value).trim();
      if (photoIdFieldTooLong(excludedIds)) {
        return skipBackupImportField("Excluded IDs exceed 255 characters - not imported");
      }
      if (excludedIds && !isValidUuidList(excludedIds)) {
        return skipBackupImportField("Import skipped invalid excluded IDs");
      }
      return queueBackupImportSetting(backupImportStateKey(entry), excludedIds);
    }
    switch (backupEntryKey(entry)) {
      case "connection.immich_url":
        var importUrl = normalizeImmichUrl(value);
        if (importUrl.length > 255) return skipBackupImportField("Immich URL exceeds 255 characters - not imported");
        if (importUrl && !isValidHttpUrl(importUrl)) return skipBackupImportField("Immich URL was invalid - not imported");
        return queueBackupImportSetting("immich_url", importUrl);
      case "connection.api_key":
        var importApiKey = value == null ? "" : String(value).trim();
        // API keys are intentionally omitted from exports. A blank value means
        // keep the destination frame's key unchanged.
        if (!importApiKey) return true;
        if (importApiKey.length > 255) return skipBackupImportField("API key exceeds 255 characters - not imported");
        queueBackupImportSetting("api_key", importApiKey);
        return true;
      case "photos.album_ids":
        var importAlbum = String(value).trim();
        if (photoIdFieldTooLong(importAlbum)) {
          return skipBackupImportField("Album IDs exceed 255 characters - not imported");
        } else if (!isValidUuidList(importAlbum)) {
          return skipBackupImportField("Import skipped invalid album IDs");
        } else {
          queueBackupImportSetting("album_ids", importAlbum);
        }
        return true;
      case "photos.album_labels":
        var importAlbumLabels = String(value).trim();
        if (photoLabelFieldTooLong(importAlbumLabels)) {
          return skipBackupImportField("Album labels exceed 255 characters - not imported");
        } else {
          queueBackupImportSetting("album_labels", importAlbumLabels);
        }
        return true;
      case "photos.person_ids":
        var importPerson = String(value).trim();
        if (photoIdFieldTooLong(importPerson)) {
          return skipBackupImportField("Person IDs exceed 255 characters - not imported");
        } else if (!isValidUuidList(importPerson)) {
          return skipBackupImportField("Import skipped invalid person IDs");
        } else {
          queueBackupImportSetting("person_ids", importPerson);
        }
        return true;
      case "photos.person_labels":
        var importPersonLabels = String(value).trim();
        if (photoLabelFieldTooLong(importPersonLabels)) {
          return skipBackupImportField("Person labels exceed 255 characters - not imported");
        } else {
          queueBackupImportSetting("person_labels", importPersonLabels);
        }
        return true;
      case "photos.tag_ids":
        var importTag = String(value).trim();
        if (photoIdFieldTooLong(importTag)) {
          return skipBackupImportField("Tag IDs exceed 255 characters - not imported");
        } else if (!isValidUuidList(importTag)) {
          return skipBackupImportField("Import skipped invalid tag IDs");
        } else {
          queueBackupImportSetting("tag_ids", importTag);
        }
        return true;
      case "photos.tag_labels":
        var importTagLabels = String(value).trim();
        if (photoLabelFieldTooLong(importTagLabels)) {
          return skipBackupImportField("Tag labels exceed 255 characters - not imported");
        } else {
          queueBackupImportSetting("tag_labels", importTagLabels);
        }
        return true;
      case "clock.timezone":
        var importedTimezone = normalizeTimezoneOption(value);
        if (TIMEZONES.indexOf(importedTimezone) === -1) {
          return skipBackupImportField("Timezone was invalid - not imported");
        }
        return queueBackupImportSetting("timezone", importedTimezone);
      case "clock.ntp_servers":
        if (Array.isArray(value) && value.length <= 3) {
          for (var ntpIndex = 0; ntpIndex < value.length; ntpIndex++) {
            var server = normalizeNtpServer(value[ntpIndex]);
            if (server.length > MAX_NTP_SERVER_LENGTH) {
              return skipBackupImportField("NTP servers exceeded 253 characters - not imported");
            }
          }
          ["ntp_server_1", "ntp_server_2", "ntp_server_3"].forEach(function (key, idx) {
            if (value[idx] === undefined) return;
            queueBackupImportSetting(key, normalizeNtpServer(value[idx]));
          });
          return true;
        }
        return skipBackupImportField("NTP servers were invalid - not imported");
      case "screen.schedule_wake_timeout":
        var wakeTimeout = normalizeScheduleWakeTimeout(value);
        var wakeValidation = validateProductSettingBackupImport(entry, wakeTimeout);
        if (!wakeValidation.ok) return skipBackupImportField(wakeValidation.message);
        return queueBackupImportSetting("schedule_wake_timeout", wakeValidation.value);
      case "screen.rotation":
        var importedRotation = String(value);
        if (screenRotationOptionsForUi().indexOf(importedRotation) !== -1) {
          queueBackupImportSetting("screen_rotation", importedRotation);
          queueBackupImportSetting("portrait_pairing", !isPortraitScreenRotation(importedRotation));
          return true;
        }
        return skipBackupImportField("Screen rotation was invalid - not imported");
      default:
        return applyGenericBackupImportField(entry, value);
    }
  }

  function importConfig() {
    if (backupImportInProgress) return;
    var fileInput = document.createElement("input");
    fileInput.type = "file";
    fileInput.accept = ".json";
    fileInput.style.display = "none";

    function removeFileInput() {
      if (fileInput.parentNode) fileInput.parentNode.removeChild(fileInput);
    }

    fileInput.addEventListener("cancel", removeFileInput);
    fileInput.addEventListener("change", function () {
      var selectedFile = fileInput.files && fileInput.files[0];
      removeFileInput();
      if (!selectedFile) return;
      var reader = new FileReader();
      reader.onload = async function () {
        try {
          var data;
          try { data = JSON.parse(String(reader.result)); } catch (_) {
            showBanner("Invalid file \u2014 could not parse JSON", "error");
            return;
          }

          var versionError = validateBackupConfigVersion(data);
          if (versionError) {
            showBanner(versionError, "error");
            return;
          }
          data = migrateBackupConfig(data);

          var restoreName = false;
          if (data.identity !== undefined) {
            if (!isObject(data.identity) || !validFrameName(data.identity.name)) {
              showBanner("Invalid frame name in backup", "error");
              return;
            }
            var choice = await chooseBackupNameRestore(data.identity.name);
            if (choice === null) return;
            restoreName = choice;
          }

          backupImportInProgress = true;
          backupImportMessages = [];
          backupImportValues = {};
          showBanner("Importing settings…", "info", 0);
          backupImportSaveTasks = [];
          var queuedCount = 0;
          var skippedCount = 0;
          var needsPhotoSourceApply = false;
          BACKUP_SCHEMA.forEach(function (entry) {
            if (!backupImportFieldPresent(data, entry)) return;
            if (applyBackupImportField(entry, backupImportFieldValue(data, entry))) {
              needsPhotoSourceApply = needsPhotoSourceApply || backupImportEntryUsesPhotoSourceApply(entry);
            } else {
              skippedCount += 1;
            }
          });

          queuedCount = Object.keys(backupImportValues).length;

          if (Object.keys(backupImportValues).length) {
            trackBackupImportSave(saveBackupImportSettings(backupImportValues), Object.keys(backupImportValues).length);
          }

          if (restoreName) {
            queuedCount += 1;
            trackBackupImportSave(saveFrameName(data.identity.name).then(function () { return { ok: true }; }));
          }

          Promise.all(backupImportSaveTasks)
            .then(function (results) {
              var failedCount = results.reduce(function (count, result) {
                return count + (result && Number(result.failedCount) || 0);
              }, 0);
              var appliedCount = queuedCount - failedCount;
              if (needsPhotoSourceApply && appliedCount) {
                return post(endpoints.apply_photo_source + "/press")
                  .then(function () {
                    return { appliedCount: appliedCount, failedCount: failedCount };
                  })
                  .catch(function () {
                    return { appliedCount: appliedCount, failedCount: failedCount + 1 };
                  });
              }
              return { appliedCount: appliedCount, failedCount: failedCount };
            })
            .then(function (summary) {
              var failedCount = summary.failedCount;
              var appliedCount = summary.appliedCount;
              backupImportInProgress = false;
              var apiKeyWasOmitted = !data.connection || !Object.prototype.hasOwnProperty.call(data.connection, "api_key") ||
                !String(data.connection.api_key == null ? "" : data.connection.api_key).trim();
              var resultMessage = backupImportSummaryMessage(appliedCount, skippedCount, failedCount);
              if (backupImportMessages.length) resultMessage += ". " + backupImportMessages.join("; ");
              if (apiKeyWasOmitted && !failedCount && !skippedCount) {
                resultMessage = S.api_key_configured
                  ? "Backup restored successfully. Your existing Immich API key was kept."
                  : "Backup restored successfully. Add your Immich API key on this screen to reconnect to Immich.";
              } else if (apiKeyWasOmitted) {
                resultMessage += ". The Immich API key was left unchanged because backups don’t include it.";
              }
              showBanner(
                resultMessage,
                failedCount ? "error" : "success"
              );
              renderSettings();
              backupImportSaveTasks = null;
              backupImportValues = null;
              backupImportMessages = [];
            })
            .catch(function () {
              backupImportInProgress = false;
              backupImportSaveTasks = null;
              backupImportValues = null;
              backupImportMessages = [];
              showBanner("Import failed. Please try again.", "error");
            });
        } catch (_) {
          backupImportInProgress = false;
          backupImportSaveTasks = null;
          backupImportValues = null;
          backupImportMessages = [];
          showBanner("Import failed. Please try again.", "error");
        }
      };
      reader.onerror = function () {
        showBanner("Could not read the selected backup file. Please try again.", "error");
      };
      reader.readAsText(selectedFile);
    });

    document.body.appendChild(fileInput);
    fileInput.click();
  }
