window.espframeNativeCards = {
  makeConnectionCard: ["immich_url", "conn_timeout"],
  makeFrequencyCard: ["interval"],
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
