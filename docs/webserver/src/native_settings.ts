import { EspframeApiClient, EspframeApiError } from "./api_client";
import type { ConfigurationSnapshot, ConfigurationValue, ConfigurationValues } from "./web_contracts";
import { button, connectFieldLabels, el, field, makeCollapsibleCard, selectFromOptions, setStatus } from "./shared_controls";

const PRODUCT_SETTINGS: Record<string, { options?: string[]; min?: number; max?: number }> = __ESPFRAME_NATIVE_PRODUCT_SETTINGS__;
const TIMEZONES: string[] = __ESPFRAME_NATIVE_TIMEZONES__;
const TIMEZONE_LABELS: Record<string, string> = __ESPFRAME_NATIVE_TIMEZONE_LABELS__;
const apiClient = new EspframeApiClient(__ESPFRAME_NATIVE_CAPABILITIES_PATH__);

function delayMs(ms: number): Promise<void> {
  return new Promise(resolve => setTimeout(resolve, ms));
}

const nativeSettingGroups: [string, [string, string][]][] = [
  ["Immich connection", [["immich_url", "Immich server URL"], ["api_key", "Replacement Immich API key"]]],
  ["Playback", [
    ["interval", "Time per photo"], ["display_mode", "Photo framing"], ["portrait_pairing", "Pair portrait photos"],
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
    ["screen_rotation", "Mounting orientation"], ["brightness_day", "Daytime brightness (%)"],
    ["brightness_night", "Nighttime brightness (%)"], ["base_tone_enabled", "Adjust display tone"],
    ["base_tone", "Display warmth (%)"], ["warm_tones_enabled", "Use warmer tones at night"],
    ["warm_tone_intensity", "Nighttime warmth (%)"],
  ]],
  ["Clock", [["show_clock", "Show clock"], ["clock_format", "Time format"], ["timezone", "Timezone"]]],
  ["Sleep schedule", [
    ["schedule_enabled", "Schedule screen off"], ["schedule_on_hour", "On hour (0–23)"],
    ["schedule_off_hour", "Off hour (0–23)"], ["schedule_wake_timeout", "Wake duration (seconds)"],
  ]],
];

function nativeSettingControl(key: string, value: ConfigurationValue): HTMLInputElement | HTMLSelectElement {
  const spec = PRODUCT_SETTINGS[key];
  const options = key === "timezone" ? TIMEZONES.slice() : (spec?.options?.slice() ?? []);
  if (options.length) {
    const current = String(value);
    if (!options.includes(current)) options.push(current);
    return selectFromOptions(options, current, function () {}, function (option) {
      if (key === "timezone") return (TIMEZONE_LABELS[option] || option).replace(/_/g, " ");
      if (key === "display_mode") return option === "Fit" ? "Show full image" : "Crop to fit";
      if (key === "screen_rotation") return option === "180" ? "Upside down (180°)" : "Normal";
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
    control.type = key === "immich_url" ? "url" : key === "api_key" ? "password" :
      key === "date_from" || key === "date_to" ? "date" : "text";
    control.value = value;
    if (key === "immich_url" || key === "api_key") control.maxLength = 256;
    if (key === "immich_url") control.required = true;
    if (key === "api_key") control.autocomplete = "new-password";
  }
  return control;
}

function nativeSettingsForm(snapshot: ConfigurationSnapshot): HTMLFormElement {
  const form = el("form");
  form.id = "native-settings";
  const fields = el("div");
  const controls = new Map<string, { element: HTMLInputElement | HTMLSelectElement; value: ConfigurationValue }>();
  let saving = false;
  const unavailable = new Set(snapshot.unavailable);
  for (const [title, definitions] of nativeSettingGroups) {
    const body = el("div");
    for (const [key, label] of definitions) {
      if (unavailable.has(key) || (key !== "api_key" && !Object.prototype.hasOwnProperty.call(snapshot.values, key))) continue;
      const value = key === "api_key" ? "" : snapshot.values[key];
      const control = nativeSettingControl(key, value);
      control.name = key;
      const row = field(label);
      if (typeof value === "boolean") {
        row.querySelector("label").prepend(control, " ");
      } else if (key === "api_key") {
        const hint = el("small");
        hint.id = "native-key-hint";
        hint.textContent = snapshot.api_key_configured ? "Leave blank to keep the current key." : "";
        control.setAttribute("aria-describedby", hint.id);
        row.append(control, hint);
      } else {
        row.appendChild(control);
      }
      controls.set(key, { element: control, value });
      body.appendChild(row);
    }
    if (body.childElementCount) fields.appendChild(makeCollapsibleCard(title, body, title !== "Playback"));
  }

  const actions = el("div", "native-settings-actions");
  const save = button("Save and restart", "btn btn-primary");
  save.type = "submit";
  save.disabled = true;
  const status = el("p", "status");
  status.setAttribute("role", "status");
  actions.append(save, status);
  form.append(fields, actions);
  connectFieldLabels(form);

  function changes() {
    const changed: ConfigurationValues = {};
    controls.forEach(({ element, value }, key) => {
      const current = element instanceof HTMLInputElement && element.type === "checkbox" ? element.checked :
        element instanceof HTMLInputElement && element.type === "number" ? element.valueAsNumber : element.value.trim();
      if (current !== value) changed[key] = current;
    });
    return changed;
  }
  const noteChange = () => { save.disabled = saving || Object.keys(changes()).length === 0; };
  const setSaving = (value: boolean) => {
    saving = value;
    controls.forEach(({ element }) => { element.disabled = value; });
    noteChange();
  };
  form.addEventListener("input", noteChange);
  form.addEventListener("change", noteChange);
  form.onsubmit = async function (event) {
    event.preventDefault();
    if (saving || !form.reportValidity()) return;
    const values = changes();
    if (!Object.keys(values).length) return;

    setSaving(true);
    setStatus(status, "Saving settings…");
    try {
      const response = await apiClient.updateSettings(values, []);
      if (!response || response.status !== "accepted") throw new Error("save_failed");
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

    const secret = controls.get("api_key");
    if (secret) secret.element.value = "";
    setStatus(status, "Settings saved. The frame is restarting…");
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

initNativeSettings();
