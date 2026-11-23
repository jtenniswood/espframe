import { EspframeApiClient, EspframeApiError } from "./api_client";
import { nativeFirmwarePanel } from "./native_firmware";
import type { ConfigurationSnapshot, ConfigurationValue, ConfigurationValues } from "./web_contracts";
import { button, connectFieldLabels, el, field, makeCollapsibleCard, selectFromOptions, setStatus } from "./shared_controls";

const PRODUCT_SETTINGS: Record<string, { options?: string[]; min?: number; max?: number }> = __ESPFRAME_NATIVE_PRODUCT_SETTINGS__;
const TIMEZONES: string[] = __ESPFRAME_NATIVE_TIMEZONES__;
const TIMEZONE_LABELS: Record<string, string> = __ESPFRAME_NATIVE_TIMEZONE_LABELS__;
const frameStatusPath = "/espframe/api/v1/status";

interface NativeFrameStatus {
  screen: string;
  playback: string;
  source: string;
  immich: string;
  notice: string;
  restarting: boolean;
}

class NativeSettingsApi extends EspframeApiClient {
  async getFrameStatus(): Promise<NativeFrameStatus | null> {
    const capabilities = await this.negotiate();
    if (capabilities?.status_path !== frameStatusPath) return null;
    const payload = await this.requestJson(frameStatusPath, { cache: "no-store" }, "frame_status_failed");
    if (!payload || typeof payload !== "object") throw new Error("invalid_frame_status");
    const status = payload as Partial<NativeFrameStatus> & { api_version?: unknown };
    if (status.api_version !== capabilities.api_version || typeof status.restarting !== "boolean" ||
        typeof status.screen !== "string" || typeof status.playback !== "string" || typeof status.source !== "string" ||
        typeof status.immich !== "string" || typeof status.notice !== "string") throw new Error("invalid_frame_status");
    return status as NativeFrameStatus;
  }
}

const apiClient = new NativeSettingsApi(__ESPFRAME_NATIVE_CAPABILITIES_PATH__);

const frameStatusLabels: Record<string, [string, Record<string, string>]> = {
  screen: ["Screen", { starting: "Starting", awake: "On", asleep: "Asleep" }],
  playback: ["Playback", { setup: "Waiting for setup", loading: "Waiting for the first photo", showing: "Showing photos",
    paused: "Paused" }],
  source: ["Photo source", { none: "None selected", all: "All photos", favorites: "Favourites", album: "Album",
    memories: "On This Day", custom: "Custom filters" }],
  immich: ["Immich", { not_configured: "Setup required", waiting_for_network: "Waiting for network or time",
    connecting: "Waiting for a response", responding: "Responding", rejected: "Access rejected",
    unreachable: "Unreachable", request_failed: "Request failed" }],
};

const frameNotices: Record<string, string> = {
  waiting_for_time: "Waiting for the frame’s clock to sync.",
  memories_empty: "No On This Day photos in the selected window.",
  memories_unavailable: "On This Day is unavailable. Check whether the Immich key can read memories.",
  incomplete_filters: "Choose values for the enabled photo filters.",
  unsupported_filters: "These filters require a newer Immich server.",
  filters_unavailable: "Check the photo filters and try again.",
  no_photos: "Immich returned no photos for this selection.",
  waiting_for_pair: "Waiting for a matching portrait pair.",
};

function initNativeFrameStatus() {
  const panel = document.getElementById("native-frame-status");
  if (!panel) return;
  const values = el("dl");
  const fields = new Map<string, HTMLElement>();
  for (const [key, [label]] of Object.entries(frameStatusLabels)) {
    const row = el("div");
    const title = el("dt");
    title.textContent = label;
    const value = el("dd");
    value.dataset.statusField = key;
    fields.set(key, value);
    row.append(title, value);
    values.append(row);
  }
  const notice = el("p", "native-frame-notice");
  notice.setAttribute("role", "status");
  notice.hidden = true;
  panel.append(values, notice);

  let timer: number | undefined;
  let busy = false;
  let supported = true;
  const setNotice = (message: string) => {
    if (notice.textContent !== message) notice.textContent = message;
    notice.hidden = !message;
  };
  const render = (status: NativeFrameStatus) => {
    for (const [key, value] of Object.entries(status)) {
      const field = fields.get(key);
      if (!field || typeof value !== "string") continue;
      const label = frameStatusLabels[key][1][value];
      const text = typeof label === "string" ? label : "Unknown";
      if (field.textContent !== text) field.textContent = text;
    }
    const detail = frameNotices[status.notice];
    setNotice(status.restarting ? "The frame is restarting." :
      status.immich === "rejected" ? "Check the Immich API key and its permissions." : typeof detail === "string" ? detail : "");
    values.hidden = status.restarting;
    panel.hidden = false;
  };
  const scheduleRefresh = (delay = 5000) => {
    window.clearTimeout(timer);
    if (supported && !document.hidden) timer = window.setTimeout(refresh, delay);
  };
  const visibilityChanged = () => {
    window.clearTimeout(timer);
    if (!document.hidden) void refresh();
  };
  const refresh = async () => {
    if (document.hidden || busy || !supported) return;
    busy = true;
    try {
      const status = await apiClient.getFrameStatus();
      if (!status) {
        supported = false;
        panel.hidden = true;
        document.removeEventListener("visibilitychange", visibilityChanged);
        return;
      }
      render(status);
    } catch (_) {
      values.hidden = true;
      setNotice("The frame is unavailable. Waiting to reconnect.");
    } finally {
      busy = false;
      scheduleRefresh();
    }
  };
  document.addEventListener("visibilitychange", visibilityChanged);
  void refresh();
}

function delayMs(ms: number): Promise<void> {
  return new Promise(resolve => setTimeout(resolve, ms));
}

const nativeSettingGroups: [string, [string, string][]][] = [
  ["Frame", [["frame_name", "Frame name"]]],
  ["Immich connection", [
    ["immich_url", "Immich server URL"], ["api_key", "Replacement Immich API key"],
    ["conn_timeout", "Connection timeout"],
  ]],
  ["Photo preparation service", [["prepared_slide_url", "Service URL"], ["prepared_slide_token", "Service access token"]]],
  ["Playback", [
    ["interval", "Time per photo"], ["photo_orientation", "Display photos"], ["display_mode", "Photo framing"],
    ["transition", "Slideshow transition"], ["transition_speed", "Transition speed"],
    ["portrait_pairing", "Pair portrait photos"],
    ["portrait_pair_framing", "Portrait pair framing"],
    ["portrait_pairs_only", "Show paired portraits only"],
    ["portrait_pairing_range", "Pairing range"],
  ]],
  ["Capture dates", [
    ["date_filter_enabled", "Limit photos by capture date"], ["date_filter_mode", "Date range"],
    ["date_from", "From"], ["date_to", "Through"], ["relative_amount", "Last"], ["relative_unit", "Unit"],
  ]],
  ["On This Day", [["memories_window", "Memories window"], ["memories_fallback", "Fall back to all photos"]]],
  ["Photo captions", [
    ["photo_metadata_date_enabled", "Show date"], ["photo_metadata_location_enabled", "Show location"],
    ["photo_metadata_date_format", "Date format"], ["photo_metadata_date_taken_format", "Date taken format"],
  ]],
  ["Display", [
    ["device_language", "Display language"],
    ["screen_rotation", "Mounting orientation"], ["brightness_day", "Daytime brightness (%)"],
    ["brightness_night", "Nighttime brightness (%)"], ["base_tone_enabled", "Adjust display tone"],
    ["base_tone", "Display warmth (%)"], ["warm_tones_enabled", "Use warmer tones at night"],
    ["warm_tone_intensity", "Nighttime warmth (%)"], ["warm_tone_override", "Warm tone until sunrise"],
  ]],
  ["Firmware", [["auto_update", "Auto Update"], ["update_frequency", "Update Frequency"]]],
  ["Clock", [["show_clock", "Show clock"], ["clock_format", "Time format"], ["timezone", "Timezone"]]],
  ["Sleep schedule", [
    ["schedule_enabled", "Schedule screen off"], ["schedule_on_hour", "On hour (0–23)"],
    ["schedule_off_hour", "Off hour (0–23)"], ["schedule_wake_timeout", "Wake duration (seconds)"],
  ]],
];

function nativeSecret(key: string): boolean {
  return key === "api_key" || key === "prepared_slide_token";
}

function nativeSettingControl(key: string, value: ConfigurationValue, developer: boolean): HTMLInputElement | HTMLSelectElement {
  const spec = PRODUCT_SETTINGS[key];
  const options = key === "timezone" ? TIMEZONES.slice() : key === "portrait_pair_framing"
    ? ["Follow photo framing", "Fill", "Fit"] : key === "screen_rotation" && !developer
      ? ["0", "180"] : (spec?.options?.slice() ?? []);
  if (options.length) {
    const current = String(value);
    if (!options.includes(current)) options.push(current);
    return selectFromOptions(options, current, function () {}, function (option) {
      if (key === "timezone") return (TIMEZONE_LABELS[option] || option).replace(/_/g, " ");
      if (key === "device_language") return new Intl.DisplayNames([option], { type: "language", languageDisplay: "standard" }).of(option) || option;
      if (key === "display_mode") return option === "Fit" ? "Show full image" : "Crop to fit";
      if (key === "portrait_pair_framing" && option !== "Follow photo framing") {
        return option === "Fill" ? "Fill each half" : "Fit both full photos";
      }
      if (key === "screen_rotation") return option === "0" ? "Normal" :
        option === "180" ? "Upside down (180°)" : "Portrait (" + option + "°)";
      return option;
    });
  }

  const control = el("input");
  if (typeof value === "boolean") {
    control.type = "checkbox";
    control.checked = value;
  } else if (typeof value === "number") {
    control.type = "number";
    if (spec?.min !== undefined) control.min = String(spec.min);
    if (spec?.max !== undefined) control.max = String(spec.max);
    control.step = "1";
    control.required = true;
    control.value = String(value);
  } else {
    const url = key === "immich_url" || key === "prepared_slide_url";
    control.type = url ? "url" : nativeSecret(key) ? "password" :
      key === "date_from" || key === "date_to" ? "date" : "text";
    control.value = value;
    if (url || nativeSecret(key)) control.maxLength = 256;
    if (key === "frame_name") {
      control.maxLength = 120;
      control.placeholder = "Use device default";
    }
    if (key === "immich_url") control.required = true;
    if (nativeSecret(key)) control.autocomplete = "new-password";
    if (key === "prepared_slide_token") control.minLength = 32;
  }
  return control;
}

function nativeSettingsForm(snapshot: ConfigurationSnapshot, savedNotice = ""): HTMLFormElement {
  const frameName = typeof snapshot.values.frame_name === "string" ? snapshot.values.frame_name : "";
  document.title = frameName ? `${frameName} · Espframe` : "Espframe settings";
  const form = el("form");
  form.id = "native-settings";
  const fields = el("div");
  const controls = new Map<string, { element: HTMLInputElement | HTMLSelectElement; value: ConfigurationValue }>();
  let saving = false;
  const unavailable = new Set(snapshot.unavailable);
  for (const [title, definitions] of nativeSettingGroups) {
    const body = el("div");
    for (const [key, label] of definitions) {
      const snapshotKey = key === "prepared_slide_token" ? "prepared_slide_url" : key;
      if (snapshotKey === "prepared_slide_url" && !snapshot.values.prepared_slide_url) continue;
      if (unavailable.has(key) || (key !== "api_key" && !Object.prototype.hasOwnProperty.call(snapshot.values, snapshotKey))) continue;
      const value = nativeSecret(key) ? "" : snapshot.values[key];
      const control = nativeSettingControl(key, value, snapshot.values.developer_features_enabled === true);
      if (key === "api_key" && control instanceof HTMLInputElement) control.required = !snapshot.api_key_configured;
      control.name = key;
      const row = field(key === "api_key" && !snapshot.api_key_configured ? "Immich API key" : label);
      if (typeof value === "boolean") {
        row.querySelector("label").prepend(control, " ");
      } else if (nativeSecret(key) || key === "prepared_slide_url" || key === "frame_name") {
        const hint = el("small");
        hint.id = `native-${key}-hint`;
        if (key === "api_key") hint.textContent = snapshot.api_key_configured ? "Leave blank to keep the current key." :
          "Enter your API key to connect Immich.";
        if (key === "frame_name") hint.textContent = "Leave blank to use the device default.";
        if (key === "prepared_slide_url") hint.textContent = "Clear the URL to return to direct Immich previews.";
        if (key === "prepared_slide_token") hint.textContent = snapshot.values.prepared_slide_token_configured ?
          "Leave blank to keep the current token. Enter a new token if you change the service URL." :
          "Enter the token from your preparation service.";
        control.setAttribute("aria-describedby", hint.id);
        row.append(control, hint);
      } else {
        row.appendChild(control);
      }
      controls.set(key, { element: control, value });
      body.appendChild(row);
    }
    if (title === "Firmware" && body.childElementCount) body.prepend(nativeFirmwarePanel());
    const openConnection = title === "Immich connection" && !snapshot.api_key_configured;
    if (body.childElementCount) fields.appendChild(makeCollapsibleCard(title, body, title !== "Playback" && !openConnection));
  }

  const actions = el("div", "native-settings-actions");
  const save = button("Save settings", "btn btn-primary");
  save.type = "submit";
  save.disabled = true;
  const status = el("p", "status");
  status.setAttribute("role", "status");
  status.textContent = savedNotice;
  actions.append(save, status);
  form.append(fields, actions);
  connectFieldLabels(form);

  function changes() {
    const changed: ConfigurationValues = {};
    controls.forEach(({ element, value }, key) => {
      const current = element instanceof HTMLInputElement && element.type === "checkbox" ? element.checked :
        element instanceof HTMLInputElement && element.type === "number" ? element.valueAsNumber :
        key === "frame_name" ? element.value.replace(/^[ \t\r\n\f\v]+|[ \t\r\n\f\v]+$/g, "") : element.value.trim();
      if (current !== value) changed[key] = current;
    });
    return changed;
  }
  const pairing = controls.get("portrait_pairing")?.element;
  const pairingOptions = [controls.get("portrait_pairs_only")?.element, controls.get("portrait_pairing_range")?.element,
    controls.get("portrait_pair_framing")?.element];
  const preparedUrl = controls.get("prepared_slide_url");
  const preparedToken = controls.get("prepared_slide_token")?.element;
  const nameInput = controls.get("frame_name")?.element;
  const noteChange = () => {
    if (pairing instanceof HTMLInputElement) {
      for (const option of pairingOptions) {
        if (option) option.disabled = saving || !pairing.checked;
      }
    }
    if (preparedToken instanceof HTMLInputElement && preparedUrl) {
      const url = preparedUrl.element.value.trim().replace(/\/+$/, "");
      preparedToken.required = url !== "" && (!snapshot.values.prepared_slide_token_configured || url !== preparedUrl.value);
    }
    if (nameInput instanceof HTMLInputElement) {
      const name = nameInput.value.replace(/^[ \t\r\n\f\v]+|[ \t\r\n\f\v]+$/g, "");
      nameInput.setCustomValidity(new TextEncoder().encode(name).length > 120 || /[\u0000-\u001f\u007f-\u009f]/.test(name) ?
        "Use up to 120 UTF-8 bytes without control characters." : "");
    }
    save.disabled = saving || Object.keys(changes()).length === 0;
  };
  const setSaving = (value: boolean) => {
    saving = value;
    controls.forEach(({ element }) => { element.disabled = value; });
    noteChange();
  };
  form.addEventListener("input", noteChange);
  form.addEventListener("change", noteChange);
  noteChange();
  form.onsubmit = async function (event) {
    event.preventDefault();
    if (saving || !form.reportValidity()) return;
    const values = changes();
    if (!Object.keys(values).length) return;

    setSaving(true);
    setStatus(status, "Saving settings…");
    let hostname = "";
    let restartRequired = true;
    try {
      const response = await apiClient.updateSettings(values, []);
      if (!response || response.status !== "accepted") throw new Error("save_failed");
      restartRequired = response.restart_required !== false;
      if (typeof response.frame_hostname === "string" && /^[a-z0-9-]{1,63}$/.test(response.frame_hostname))
        hostname = response.frame_hostname + ".local";
    } catch (error) {
      const label = error instanceof EspframeApiError && error.field ?
        form.querySelector<HTMLLabelElement>(`label[for="${controls.get(error.field)?.element.id || ""}"]`)?.textContent : "";
      const message = label ? `Check the value for ${label}.` :
        error instanceof EspframeApiError && error.kind === "validation" ? "Check your settings and try again." :
        "Settings could not be confirmed. Check the frame connection and try again.";
      setStatus(status, message, "red");
      setSaving(false);
      return;
    }

    controls.forEach(({ element }, key) => { if (nativeSecret(key)) element.value = ""; });
    if (!restartRequired) {
      try {
        form.replaceWith(nativeSettingsForm(await apiClient.getConfigurationSnapshot(), "Settings saved."));
      } catch (_) {
        setStatus(status, "Settings saved. Reload this page to see the current values.");
        setSaving(false);
      }
      return;
    }
    setStatus(status, "Settings saved. The frame is restarting…");
    if (hostname && hostname !== window.location.hostname) {
      const link = el("a");
      const address = new URL("/", window.location.href);
      address.hostname = hostname;
      link.href = address.href;
      link.textContent = `Open ${hostname}`;
      actions.appendChild(link);
    }
    await delayMs(2500);
    for (let attempt = 0; attempt < 5; attempt++) {
      try {
        form.replaceWith(nativeSettingsForm(await apiClient.getConfigurationSnapshot()));
        return;
      } catch (_) {
        await delayMs(1500);
      }
    }
    setStatus(status, "Settings saved. Reload the page once the frame reconnects.");
    actions.appendChild(button("Reload settings", "btn btn-secondary", () => window.location.reload()));
  };
  return form;
}

async function initNativeSettings() {
  const content = document.getElementById("native-settings-content");
  if (!content) return;
  const status = el("p", "status");
  status.setAttribute("role", "status");
  status.textContent = "Loading settings…";
  content.replaceChildren(status);
  try {
    content.replaceChildren(nativeSettingsForm(await apiClient.getConfigurationSnapshot()));
  } catch (_) {
    status.textContent = "The frame settings could not be loaded.";
    content.appendChild(button("Try again", "btn btn-secondary", initNativeSettings));
  }
}

initNativeFrameStatus();
initNativeSettings();
