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
      version: 1,
      exported_at: new Date().toISOString()
    };
    BACKUP_SCHEMA.forEach(function (entry) {
      if (!entry || !entry.group || !entry.field) return;
      if (!data[entry.group]) data[entry.group] = {};
      data[entry.group][entry.field] = backupExportFieldValue(entry);
    });
    return data;
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

        if (!data.version) {
          showBanner("Invalid config file \u2014 missing version", "error");
          return;
        }

        var c = data.connection || {};
        var p = data.photos || {};
        var f = data.frequency || {};
        var upd = data.firmware_updates || {};
        var clk = data.clock || {};
        var scr = data.screen || {};

        if (c.immich_url !== undefined) {
          S.immich_url = normalizeImmichUrl(c.immich_url);
          postTextValueSet(endpoints.immich_url + "/set", S.immich_url, true);
        }
        if (c.api_key !== undefined) {
          S.api_key = c.api_key;
          postTextValueSet(endpoints.api_key + "/set", c.api_key, true);
        }

        if (p.source !== undefined) {
          S.photo_source = p.source;
          post(endpoints.photo_source + "/set", { option: p.source });
        }
        if (p.album_ids !== undefined) {
          var importAlbum = String(p.album_ids).trim();
          if (photoIdFieldTooLong(importAlbum)) {
            showBanner("Album IDs exceed 255 characters - not imported", "error");
          } else if (!isValidUuidList(importAlbum)) {
            showBanner("Import skipped invalid album IDs", "error");
          } else {
            S.album_ids = importAlbum;
            postTextValueSet(endpoints.album_ids + "/set", importAlbum);
          }
        }
        if (p.album_labels !== undefined) {
          var importAlbumLabels = String(p.album_labels).trim();
          if (photoLabelFieldTooLong(importAlbumLabels)) {
            showBanner("Album labels exceed 255 characters - not imported", "error");
          } else {
            S.album_labels = importAlbumLabels;
            postTextValueSet(endpoints.album_labels + "/set", importAlbumLabels);
          }
        }
        if (p.person_ids !== undefined) {
          var importPerson = String(p.person_ids).trim();
          if (photoIdFieldTooLong(importPerson)) {
            showBanner("Person IDs exceed 255 characters - not imported", "error");
          } else if (!isValidUuidList(importPerson)) {
            showBanner("Import skipped invalid person IDs", "error");
          } else {
            S.person_ids = importPerson;
            postTextValueSet(endpoints.person_ids + "/set", importPerson);
          }
        }
        if (p.person_labels !== undefined) {
          var importPersonLabels = String(p.person_labels).trim();
          if (photoLabelFieldTooLong(importPersonLabels)) {
            showBanner("Person labels exceed 255 characters - not imported", "error");
          } else {
            S.person_labels = importPersonLabels;
            postTextValueSet(endpoints.person_labels + "/set", importPersonLabels);
          }
        }
        if (p.portrait_pairing !== undefined) {
          S.portrait_pairing = p.portrait_pairing;
          post(endpoints.portrait_pairing + (p.portrait_pairing ? "/turn_on" : "/turn_off"));
        }
        if (p.display_mode !== undefined) {
          S.display_mode = p.display_mode;
          post(endpoints.display_mode + "/set", { option: p.display_mode });
        }
        if (p.orientation !== undefined) {
          S.photo_orientation = p.orientation;
          post(endpoints.photo_orientation + "/set", { option: p.orientation });
        }
        if (p.date_filter_enabled !== undefined) {
          S.date_filter_enabled = p.date_filter_enabled;
          post(endpoints.date_filter_enabled + (p.date_filter_enabled ? "/turn_on" : "/turn_off"));
        }
        if (p.date_filter_mode !== undefined) {
          S.date_filter_mode = p.date_filter_mode;
          post(endpoints.date_filter_mode + "/set", { option: p.date_filter_mode });
        }
        if (p.date_from !== undefined) {
          S.date_from = p.date_from;
          post(endpoints.date_from + "/set", { value: p.date_from });
        }
        if (p.date_to !== undefined) {
          S.date_to = p.date_to;
          post(endpoints.date_to + "/set", { value: p.date_to });
        }
        if (p.relative_amount !== undefined) {
          S.relative_amount = p.relative_amount;
          post(endpoints.relative_amount + "/set", { value: p.relative_amount });
        }
        if (p.relative_unit !== undefined) {
          S.relative_unit = p.relative_unit;
          post(endpoints.relative_unit + "/set", { option: p.relative_unit });
        }

        if (f.interval !== undefined) {
          S.interval = f.interval;
          post(endpoints.interval + "/set", { option: f.interval });
        }
        if (f.conn_timeout !== undefined) {
          S.conn_timeout = f.conn_timeout;
          post(endpoints.conn_timeout + "/set", { option: f.conn_timeout });
        }

        if (upd.auto_update !== undefined) {
          S.auto_update = upd.auto_update;
          post(endpoints.auto_update + (upd.auto_update ? "/turn_on" : "/turn_off"));
        }
        if (upd.beta_channel !== undefined) {
          S.beta_channel = upd.beta_channel;
          post(endpoints.beta_channel + (upd.beta_channel ? "/turn_on" : "/turn_off"));
        }
        if (upd.update_frequency !== undefined) {
          S.update_frequency = upd.update_frequency;
          post(endpoints.update_frequency + "/set", { option: upd.update_frequency });
        }
        if (upd.manifest_url !== undefined) {
          var importManifestUrl = normalizeFirmwareManifestUrl(upd.manifest_url);
          if (importManifestUrl && !isValidHttpUrl(importManifestUrl)) {
            showBanner("Stable firmware URL was invalid - not imported", "error");
          } else {
            S.firmware_manifest_url = importManifestUrl;
            postTextValueSet(endpoints.firmware_manifest_url + "/set", importManifestUrl);
          }
        }
        if (upd.beta_manifest_url !== undefined) {
          var importBetaManifestUrl = normalizeFirmwareManifestUrl(upd.beta_manifest_url);
          if (importBetaManifestUrl && !isValidHttpUrl(importBetaManifestUrl)) {
            showBanner("Beta firmware URL was invalid - not imported", "error");
          } else {
            S.firmware_beta_manifest_url = importBetaManifestUrl;
            postTextValueSet(endpoints.firmware_beta_manifest_url + "/set", importBetaManifestUrl);
          }
        }

        if (clk.show !== undefined) {
          S.show_clock = clk.show;
          post(endpoints.show_clock + (clk.show ? "/turn_on" : "/turn_off"));
        }
        if (clk.format !== undefined) {
          S.clock_format = clk.format;
          post(endpoints.clock_format + "/set", { option: clk.format });
        }
        if (clk.timezone !== undefined) {
          S.timezone = normalizeTimezoneOption(clk.timezone);
          post(endpoints.timezone + "/set", { option: S.timezone });
        }
        if (Array.isArray(clk.ntp_servers)) {
          ["ntp_server_1", "ntp_server_2", "ntp_server_3"].forEach(function (key, idx) {
            if (clk.ntp_servers[idx] === undefined) return;
            saveNtpServer(key, clk.ntp_servers[idx]);
          });
        }

        if (scr.brightness_day !== undefined) {
          S.brightness_day = scr.brightness_day;
          post(endpoints.brightness_day + "/set", { value: scr.brightness_day });
        }
        if (scr.brightness_night !== undefined) {
          S.brightness_night = scr.brightness_night;
          post(endpoints.brightness_night + "/set", { value: scr.brightness_night });
        }

        if (scr.schedule_enabled !== undefined) {
          S.schedule_enabled = scr.schedule_enabled;
          post(endpoints.schedule_enabled + (scr.schedule_enabled ? "/turn_on" : "/turn_off"));
        }
        if (scr.schedule_on_hour !== undefined) {
          S.schedule_on_hour = scr.schedule_on_hour;
          post(endpoints.schedule_on_hour + "/set", { value: scr.schedule_on_hour });
        }
        if (scr.schedule_off_hour !== undefined) {
          S.schedule_off_hour = scr.schedule_off_hour;
          post(endpoints.schedule_off_hour + "/set", { value: scr.schedule_off_hour });
        }
        if (scr.schedule_wake_timeout !== undefined) {
          S.schedule_wake_timeout = normalizeScheduleWakeTimeout(scr.schedule_wake_timeout);
          postScheduleWakeTimeout(S.schedule_wake_timeout);
        }

        if (scr.base_tone_enabled !== undefined) {
          S.base_tone_enabled = scr.base_tone_enabled;
          post(endpoints.base_tone_enabled + (scr.base_tone_enabled ? "/turn_on" : "/turn_off"));
        }
        if (scr.base_tone !== undefined) {
          S.base_tone = scr.base_tone;
          post(endpoints.base_tone + "/set", { value: scr.base_tone });
        }
        if (scr.warm_tones_enabled !== undefined) {
          S.warm_tones_enabled = scr.warm_tones_enabled;
          post(endpoints.warm_tones_enabled + (scr.warm_tones_enabled ? "/turn_on" : "/turn_off"));
        }
        if (scr.warm_tone_intensity !== undefined) {
          S.warm_tone_intensity = scr.warm_tone_intensity;
          post(endpoints.warm_tone_intensity + "/set", { value: scr.warm_tone_intensity });
        }
        if (scr.warm_tone_override !== undefined) {
          S.warm_tone_override = scr.warm_tone_override;
          post(endpoints.warm_tone_override + (scr.warm_tone_override ? "/turn_on" : "/turn_off"));
        }
        if (scr.rotation !== undefined) {
          var importedRotation = String(scr.rotation);
          if (screenRotationOptionsForUi().indexOf(importedRotation) !== -1) {
            S.screen_rotation = importedRotation;
            post(endpoints.screen_rotation + "/set", { option: S.screen_rotation });
          }
        }

        showBanner("Settings imported successfully", "success");
        renderSettings();
      };
      reader.readAsText(fileInput.files[0]);
    });

    document.body.appendChild(fileInput);
    fileInput.click();
    document.body.removeChild(fileInput);
  }
