window.espframeNativeCards = {
  makeConnectionCard: ["immich_url", "conn_timeout"],
  makeFrequencyCard: ["interval"],
  makeMemoriesCard: ["photo_source", "memories_window", "memories_fallback"],
  makeFiltersCard: ["photo_source", "immich_filter_support", "album_labels", "excluded_tag_labels"],
  makePortraitPairingCard: ["portrait_pairing", "portrait_pairs_only", "portrait_pairing_range"],
  makeLayoutCard: ["photo_orientation", "display_mode", "transition", "transition_speed"],
  makeMetadataCard: ["photo_metadata_date_enabled", "photo_metadata_location_enabled",
    "photo_metadata_date_format", "photo_metadata_date_taken_format"],
  makeScreenBrightnessCard: ["brightness_day", "brightness_night"],
  makeScreenToneCard: ["base_tone_enabled", "base_tone", "warm_tones_enabled", "warm_tone_intensity", "warm_tone_override"],
  makeNightScheduleCard: ["schedule_enabled", "schedule_on_hour", "schedule_off_hour", "schedule_wake_timeout"],
  makeRotationCard: ["screen_rotation"],
  makeLanguageCard: ["device_language"],
  makeClockCard: ["show_clock", "clock_format", "timezone", "ntp_server_1", "ntp_server_2", "ntp_server_3"],
  makeFrameNameCard: ["frame_name"],
  makeFirmwareCard: ["auto_update", "update_frequency"],
  makeBackupCard: [],
  makeDeviceRebootCard: [],
  makeResetCard: [],
};

window.espframeNativeUrl = function (url) {
  if (!/^https?:\/\//.test(url)) return url;
  const hasPath = url.indexOf("/", url.indexOf("://") + 3) >= 0;
  return hasPath && url.endsWith("/api") ? url : url + "/api";
};

window.espframeShowLogs = (() => {
  let active = false;
  let reading = false;
  let timer;
  let boot;
  let after = 0;

  async function read(appendLog) {
    if (!active || reading) return;
    reading = true;
    let delay = 1000;
    try {
      if (document.hidden) return;
      const response = await fetch("/espframe/api/v1/logs?after=" + after, {
        cache: "no-store", signal: AbortSignal.timeout(2500),
      });
      if (!active || !response.ok) return;
      const page = await response.json();
      if (!Number.isInteger(page.boot) || !Array.isArray(page.lines)) return;
      if (boot !== undefined && boot !== page.boot && after !== 0) {
        boot = page.boot;
        after = 0;
        delay = 0;
        return;
      }
      boot = page.boot;
      const initial = after;
      for (const line of page.lines) {
        if (!Number.isInteger(line.id) || line.id <= after || typeof line.message !== "string") continue;
        appendLog(line.message, line.level);
        after = line.id;
      }
      if (page.more === true && after !== initial) delay = 30;
    } catch (_) {
      // The frame can briefly disappear during an update or restart.
    } finally {
      reading = false;
      if (active) timer = setTimeout(() => read(appendLog), delay);
    }
  }

  return (visible, appendLog) => {
    if (visible === active) return;
    active = visible;
    clearTimeout(timer);
    if (active) void read(appendLog);
  };
})();

window.espframeNativeBackupSchema = function (schema) {
  schema.push({ group: "photos", field: "portrait_pair_framing", state_keys: ["portrait_pair_framing"] });
};

window.espframeNativeBackupDefaults = function (original, migrated) {
  const photos = migrated.photos;
  if (photos && !Object.hasOwn(photos, "portrait_pair_framing") &&
      (original.version < 3 || Object.hasOwn(original, "firmware_updates")) &&
      (typeof photos.portrait_pairing === "boolean" || ["Fill", "Fit"].includes(photos.display_mode))) {
    return { ...migrated, photos: { ...photos, portrait_pair_framing: "Fill" } };
  }
  return migrated;
};

window.espframeNativeAfterReset = async function (mode, address, showBanner) {
  if (mode === "factory") {
    showBanner("Factory reset accepted. Follow the Wi-Fi setup instructions on the frame after it restarts.", "info", 0);
    return;
  }
  showBanner("Settings reset. The frame is restarting…", "info", 0);
  try {
    await window.espframeNativeReconnect(async () => {
      const response = await fetch("/espframe/api/v1/reset", { cache: "no-store", signal: AbortSignal.timeout(2500) });
      if (!response.ok || (await response.json()).status !== "ready") throw Error("reset_pending");
    });
    location.reload();
  } catch (_) {
    if (address && /^(?:\d{1,3}\.){3}\d{1,3}$/.test(address) && address !== location.hostname) {
      location.href = "http://" + address + "/admin";
      return;
    }
    showBanner("Settings reset. Reload this page when the frame reconnects.", "info", 0);
  }
};

window.espframeWatchFilters = async function (state, readSettings, renderSettings) {
  if (state.immich_filter_support !== "unknown") return;
  for (let attempt = 0; attempt < 8; ++attempt) {
    await new Promise(resolve => setTimeout(resolve, 2000));
    try {
      const { values } = await readSettings();
      if (values.immich_filter_support === "structured" || values.immich_filter_support === "legacy") {
        state.immich_filter_support = values.immich_filter_support;
        renderSettings();
        return;
      }
    } catch (_) {
      // Reconnects are expected while the frame is starting.
    }
  }
};

window.espframePairFraming = function (field, select, current, save) {
  const labels = { "Follow photo framing": "Follow photo framing", Fill: "Fill each half", Fit: "Fit both full photos" };
  const control = field("Portrait pair framing");
  control.appendChild(select(Object.keys(labels), current, save, value => labels[value]));
  return control;
};

window.espframeNativeReconnect = async function (readSettings) {
  const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
  await wait(2500);
  for (let attempt = 0; ; ++attempt) {
    try {
      return await readSettings();
    } catch (error) {
      if (attempt >= 4) throw error;
      await wait(1500);
    }
  }
};

window.espframeNativeFirmware = function (card, state, refreshUi, compareVersions, showBanner) {
  const path = "/api/firmware/releases";
  const versionPattern = /^v\d+\.\d+\.\d+-preview\.\d+$/;
  let supported = false;
  let requestInFlight = false;
  let selectedVersion = "";
  let requestGeneration = 0;
  let observedRelease = false;
  let timer;

  function schedule() {
    clearTimeout(timer);
    if (card.isConnected) timer = setTimeout(read, 3000);
  }

  async function read() {
    if (!card.isConnected || requestInFlight) return;
    const generation = requestGeneration;
    const isCurrent = () => card.isConnected && generation === requestGeneration;
    try {
      const response = await fetch(path, { cache: "no-store", signal: AbortSignal.timeout(2500) });
      if (!response.ok) throw Error("release_status_unavailable");
      const release = await response.json();
      if (!isCurrent()) return;
      supported = release.available === true;
      state.installed_version = typeof release.current === "string" ? release.current : "";
      state.firmware_version_options = Array.isArray(release.versions) ? release.versions
        .filter(version => typeof version === "string" && versionPattern.test(version))
        .map(version => ({ version })) : [];
      state.firmware_versions_loaded = true;
      state.latest_version = state.firmware_version_options[0]?.version || "";
      state.update_available = compareVersions(state.latest_version, state.installed_version) > 0;
      state.firmware_checking = release.status === "checking";
      state.firmware_installing = release.status === "installing" || release.status === "restarting";
      state.firmware_install_error = release.status === "unavailable" ? "Could not check for a firmware update." :
        release.status === "failed" ? "The frame could not install that release." :
        supported ? "" : "Release updates require a versioned Native Preview.";
      if (selectedVersion && selectedVersion === state.installed_version) {
        const boot = await fetch("/api/firmware", { cache: "no-store", signal: AbortSignal.timeout(2500) });
        const verified = boot.ok && (await boot.json()).boot_verified === true;
        if (!isCurrent()) return;
        if (verified) {
          selectedVersion = "";
          showBanner("Firmware update complete.", "success");
        } else {
          state.firmware_installing = true;
        }
      } else if (selectedVersion && release.status === "idle") {
        selectedVersion = "";
        state.firmware_install_error = "The frame restarted on the previous firmware.";
      }
      if (release.status === "failed" || release.status === "unavailable") selectedVersion = "";
      refreshUi();
      const shouldCheck = !observedRelease && supported && release.status === "idle";
      observedRelease = true;
      if (shouldCheck) void request("");
    } catch (_) {
      if (!isCurrent()) return;
      supported = false;
      state.firmware_checking = false;
      state.firmware_installing = !!selectedVersion;
      state.firmware_install_error = selectedVersion ? "" : "Firmware release information is unavailable.";
      refreshUi();
    } finally {
      if (isCurrent()) schedule();
    }
  }

  async function request(version) {
    if (!card.isConnected || !supported || requestInFlight) return false;
    ++requestGeneration;
    requestInFlight = true;
    clearTimeout(timer);
    state.firmware_install_error = "";
    state.firmware_checking = !version;
    state.firmware_installing = !!version;
    refreshUi();
    try {
      const response = await fetch(path, {
        method: "POST", headers: { "Content-Type": "text/plain" }, body: version,
        signal: AbortSignal.timeout(4000),
      });
      if (!response.ok) throw Error("release_request_failed");
      selectedVersion = version;
    } catch (_) {
      state.firmware_checking = false;
      state.firmware_installing = false;
      state.firmware_install_error = "Could not start the firmware request.";
      refreshUi();
      schedule();
      return false;
    } finally {
      requestInFlight = false;
    }
    await read();
    return true;
  }

  // Rendering attaches the card to its tab before the first status read.
  setTimeout(read, 0);
  return { request, isAvailable: () => supported };
};
