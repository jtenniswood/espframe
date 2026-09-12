  interface FrameIdentitySnapshot {
    name: string;
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
        typeof data.ip_address !== "string" || typeof data.restart_required !== "boolean") {
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

  function makeFrameNameCard(): HTMLElement | null {
    if (!frameIdentity) return null;
    var body = el("div");
    var field = el("div", "field");
    var label = document.createElement("label");
    label.textContent = "Frame name";
    label.htmlFor = "frame-name";
    var input = document.createElement("input");
    input.type = "text";
    input.id = "frame-name";
    input.value = frameNameDraft === null ? frameIdentity.name : frameNameDraft;
    input.placeholder = frameIdentity.friendly_name;
    input.disabled = frameIdentityBusy;
    input.addEventListener("input", function () { frameNameDraft = input.value; });
    field.append(label, input);
    body.appendChild(field);
    var help = el("p", "hint");
    help.textContent = "Updates the web title, network hostname and Home Assistant device name after restart. Leave blank to restore firmware defaults.";
    body.appendChild(help);
    var save = button("Save name", "btn btn-primary btn-sm", function () {
      save.disabled = true;
      input.disabled = true;
      saveFrameName(input.value).then(function () {
        showBanner("Frame name saved" + (frameIdentity.restart_required ? ". Restart to apply it." : "."), "success");
      }).catch(function () {
        // The card retains the draft and displays the save error.
      }).finally(function () { renderSettingsAfterEditing(); });
    });
    save.disabled = frameIdentityBusy;
    body.appendChild(save);
    if (frameIdentityError) {
      var error = el("p", "hint");
      error.setAttribute("role", "alert");
      error.textContent = frameIdentityError;
      body.appendChild(error);
    }
    var address = el("p", "hint frame-name-address");
    address.textContent = (frameIdentity.restart_required ? "After restart: " : "Address: ");
    var link = document.createElement("a");
    link.href = "http://" + frameIdentity.hostname + ".local" + (location.port ? ":" + location.port : "") + "/";
    link.textContent = frameIdentity.hostname + ".local";
    address.appendChild(link);
    if (frameIdentity.ip_address) address.appendChild(document.createTextNode(" · Current IP: " + frameIdentity.ip_address));
    body.appendChild(address);
    if (frameIdentity.restart_required) {
      var restart = button("Restart to apply name", "btn btn-secondary btn-sm", function () {
        restart.disabled = true;
        restart.textContent = "Restarting… Use the address above to reconnect";
        post(endpoints.reboot_screen + "/press").catch(function () {
          restart.disabled = false;
          restart.textContent = "Restart to apply name";
        });
      });
      body.appendChild(restart);
    }
    return makeCollapsibleCard("Frame name", body, !frameIdentity.restart_required && !frameIdentityError && frameNameDraft === null);
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
