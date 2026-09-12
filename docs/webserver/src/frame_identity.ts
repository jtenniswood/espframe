  interface FrameIdentitySnapshot {
    name: string;
    mac_suffix?: string;
    friendly_name: string;
    hostname: string;
    ip_address: string;
    restart_required: boolean;
  }

  var frameIdentity: FrameIdentitySnapshot | null = null;
  var frameNameDraft: string | null = null;
  var frameIdentityBusy = false;
  var frameIdentityError = "";

  function validFrameName(value: unknown): value is string {
    if (typeof value !== "string") return false;
    var name = value.replace(/^[ \t\r\n\f\v]+|[ \t\r\n\f\v]+$/g, "");
    // encodeURIComponent rejects lone surrogates, which TextEncoder replaces.
    try { encodeURIComponent(name); } catch (_) { return false; }
    return new TextEncoder().encode(name).length <= 120 && !/[\u0000-\u001f\u007f-\u009f]/.test(name);
  }

  async function requestFrameIdentity(name?: string): Promise<FrameIdentitySnapshot> {
    var options: RequestInit = { cache: "no-store" };
    if (name !== undefined) {
      if (!validFrameName(name)) throw new Error("Use up to 120 UTF-8 bytes without control characters.");
      options.method = "POST";
      options.headers = { "Content-Type": "application/x-www-form-urlencoded" };
      options.body = new URLSearchParams({ name: name }).toString();
    }
    var response = await fetch("/espframe/api/v1/identity", options);
    if (!response.ok) throw new Error(name === undefined ? "Frame name unavailable" : "Frame name could not be saved. Please retry.");
    var data: unknown = await response.json();
    if (!isObject(data) || !validFrameName(data.name) || typeof data.friendly_name !== "string" ||
        typeof data.hostname !== "string" || !/^[a-z0-9-]{1,63}$/.test(data.hostname) ||
        typeof data.ip_address !== "string" || typeof data.restart_required !== "boolean" ||
        (data.mac_suffix !== undefined && (typeof data.mac_suffix !== "string" || !/^[a-f0-9]{4}$/.test(data.mac_suffix)))) {
      throw new Error("Frame name unavailable");
    }
    return data as unknown as FrameIdentitySnapshot;
  }

  function updateFrameTitle(): void {
    if (!frameIdentity) return;
    document.title = frameIdentity.friendly_name + " · EspFrame";
    var brand = document.querySelector<HTMLElement>(".sp-brand");
    if (brand) brand.textContent = frameIdentity.name || "EspFrame";
  }

  async function loadFrameIdentity(): Promise<void> {
    try {
      frameIdentity = await requestFrameIdentity();
      updateFrameTitle();
      if (rendered) renderSettingsAfterEditing();
    } catch (_) {
      // A hosted app may be used with firmware predating this endpoint.
    }
  }

  async function saveFrameName(name: string): Promise<void> {
    if (frameIdentityBusy) throw new Error("A frame name save is already in progress.");
    frameIdentityBusy = true;
    frameIdentityError = "";
    try {
      frameIdentity = await requestFrameIdentity(name);
      frameNameDraft = null;
      updateFrameTitle();
    } catch (error) {
      frameIdentityError = error instanceof Error ? error.message : "Frame name could not be saved.";
      throw error;
    } finally {
      frameIdentityBusy = false;
    }
  }

  // Match Espcontrol's naming form and reconnect dialog, using Espframe controls.
  function previewFrameHostname(name: string): string | null {
    if (!frameIdentity) return null;
    if (name === frameIdentity.name) return frameIdentity.hostname;
    if (!name || !frameIdentity.mac_suffix) return null;
    var slug = name.replace(/[A-Z]/g, function (c) { return c.toLowerCase(); }).replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "frame";
    return slug.slice(0, 19).replace(/-$/, "") + "-" + frameIdentity.mac_suffix;
  }

  function showFrameReconnectDialog(value: FrameIdentitySnapshot): HTMLParagraphElement {
    var dialog = document.createElement("dialog");
    dialog.className = "frame-name-dialog frame-reconnect-dialog";
    var title = document.createElement("h3");
    title.id = "frame-reconnect-title";
    title.textContent = "Frame name saved";
    dialog.setAttribute("aria-labelledby", title.id);
    var note = document.createElement("p");
    note.setAttribute("role", "status");
    note.textContent = "The frame is restarting. Reopen it at the new address.";
    var address = document.createElement("a");
    var port = location.port ? ":" + location.port : "";
    address.href = "http://" + value.hostname + ".local" + port + "/";
    address.textContent = value.hostname + ".local";
    dialog.append(title, note, address);
    if (/^(?:\d{1,3}\.){3}\d{1,3}$/.test(value.ip_address) &&
        value.ip_address.split(".").every(function (part) { return Number(part) <= 255; })) {
      var ip = document.createElement("a");
      ip.href = "http://" + value.ip_address + port + "/";
      ip.textContent = value.ip_address;
      dialog.append(document.createElement("br"), ip);
    }
    dialog.append(document.createElement("br"), button("Close", "btn btn-secondary frame-name-button", function () { dialog.close(); dialog.remove(); }));
    dialog.addEventListener("close", function () { dialog.remove(); });
    document.body.appendChild(dialog);
    dialog.showModal();
    return note;
  }

  function makeFrameNameCard(): HTMLElement | null {
    if (!frameIdentity) return null;
    var body = el("div");
    var label = document.createElement("label");
    label.className = "frame-name-label";
    label.textContent = "Frame Name";
    label.htmlFor = "frame-name";
    var input = document.createElement("input");
    input.type = "text";
    input.id = "frame-name";
    input.value = frameNameDraft === null ? frameIdentity.name : frameNameDraft;
    input.placeholder = "e.g. Living Room";
    input.disabled = frameIdentityBusy;
    var preview = el("div", "frame-name-info");
    var icon = el("span", "frame-name-info-icon");
    icon.setAttribute("aria-hidden", "true");
    icon.innerHTML = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="9"/><path d="M12 11v6m0-10v2"/></svg>';
    var previewText = document.createElement("span");
    preview.append(icon, previewText);
    var error = el("p", "field-error");
    error.setAttribute("role", "alert");
    error.textContent = frameIdentityError;
    var save = button("Save & Restart", "btn btn-secondary frame-name-button", async function () {
      save.disabled = true;
      input.disabled = true;
      try {
        await configurationUpdateQueue;
        await saveFrameName(input.value);
        if (frameIdentity.restart_required) {
          var message = showFrameReconnectDialog(frameIdentity);
          try { await post(endpoints.reboot_screen + "/press"); }
          catch (_) {
            message.textContent = "Name saved, but the restart failed. Close this dialog and choose Save & Restart to retry.";
            frameIdentityError = "Name saved, but the restart failed. Please retry.";
          }
        }
      } catch (_) {
        // Preserve the draft and display the save error; failed saves never reboot.
      } finally {
        input.disabled = false;
        sync();
        renderSettingsAfterEditing();
      }
    });
    function sync(): void {
      var name = input.value.replace(/^[ \t\r\n\f\v]+|[ \t\r\n\f\v]+$/g, "");
      var valid = validFrameName(name);
      error.textContent = valid ? frameIdentityError : "Use up to 120 UTF-8 bytes without control characters.";
      save.disabled = frameIdentityBusy || !valid || (name === frameIdentity.name && !frameIdentity.restart_required);
      var hostname = previewFrameHostname(name);
      if (hostname) {
        var address = document.createElement("code");
        address.textContent = hostname + ".local";
        previewText.replaceChildren("Your device will show as ", address, " on your network");
      } else {
        previewText.textContent = name
          ? "Your device's new address will be shown after saving"
          : "Your device will use its original firmware name and address on your network";
      }
    }
    input.addEventListener("input", function () { frameNameDraft = input.value; frameIdentityError = ""; sync(); });
    var row = el("div", "frame-name-row");
    row.append(input, save);
    body.append(label, row, error, preview);
    sync();
    return makeCollapsibleCard("Frame Name", body, !frameIdentity.restart_required && !frameIdentityError && frameNameDraft === null);
  }

  function chooseBackupNameRestore(name: string): Promise<boolean | null> {
    return new Promise(function (resolve) {
      var dialog = document.createElement("dialog");
      dialog.className = "frame-name-dialog";
      dialog.setAttribute("aria-label", "Import backup");
      var label = document.createElement("label");
      var checkbox = document.createElement("input");
      checkbox.type = "checkbox";
      checkbox.id = "restore-frame-name";
      checkbox.disabled = !frameIdentity;
      label.append(checkbox, document.createTextNode(" Also restore frame name: " + (name || "Firmware default")));
      var help = document.createElement("p");
      help.textContent = frameIdentity
        ? "Unchecked keeps this frame's name. Restoring uses this frame's own MAC suffix and requires a restart."
        : "Update this frame's firmware to restore names. Other settings can still be imported.";
      function finish(value: boolean | null): void { dialog.close(); dialog.remove(); resolve(value); }
      dialog.append(label, help,
        button("Import backup", "btn btn-primary", function () { finish(checkbox.checked); }),
        button("Cancel", "btn btn-secondary", function () { finish(null); }));
      dialog.addEventListener("cancel", function (event) { event.preventDefault(); finish(null); });
      document.body.appendChild(dialog);
      dialog.showModal();
    });
  }
