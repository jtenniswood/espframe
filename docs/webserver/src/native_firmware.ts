import { button, el, field } from "./shared_controls";

const releasesPath = "/api/firmware/releases";
const versionPattern = /^v\d+\.\d+\.\d+-preview\.\d+$/;
interface ReleaseStatus {
  current: string;
  versions: string[];
  status: string;
  available: boolean;
}

const messages: Record<string, string> = {
  checking: "Checking for updates…",
  unavailable: "Could not check for Native Preview releases.",
  installing: "Downloading and installing firmware…",
  restarting: "Waiting for the frame to restart…",
  failed: "The frame could not install that release.",
};

function newer(candidate: string, current: string): boolean {
  if (!versionPattern.test(current)) return false;
  const parts = (version: string) => version.match(/\d+/g)!.map(Number);
  const installed = parts(current);
  for (const [index, number] of parts(candidate).entries()) {
    if (number !== installed[index]) return number > installed[index];
  }
  return false;
}

export function nativeFirmwarePanel(): HTMLElement {
  const panel = el("div", "native-firmware");
  const label = (name: string) => {
    const row = el("div", "fw-row");
    const value = el("span", "fw-label");
    value.textContent = "—";
    row.append(name, value);
    panel.append(row);
    return value;
  };
  const current = label("Current");
  const available = label("Available");
  const status = el("p", "fw-status");
  status.setAttribute("role", "status");
  const check = button("Check for Update", "btn btn-secondary btn-sm");
  const install = button("Install Update", "btn btn-primary btn-sm");
  const installed = button("Install version", "btn btn-secondary btn-sm");
  const actions = el("div", "fw-actions");
  actions.append(check, install);
  panel.append(actions);

  const history = field("Previous versions");
  const versions = el("select", "select");
  versions.setAttribute("aria-label", "Previous versions");
  history.append(versions, installed);
  panel.append(history, status);
  const localFile = el("a");
  localFile.href = "/settings#firmware";
  localFile.textContent = "Install firmware from a file";
  panel.append(localFile);

  let latest = "";
  let posting = false;
  let timer: number | undefined;
  let target = "";

  function clearActions() {
    check.disabled = true;
    install.hidden = true;
    history.hidden = true;
  }

  function show(release: ReleaseStatus) {
    current.textContent = release.current || "Unknown";
    latest = release.versions[0] || "";
    available.textContent = latest || "—";
    const updating = ["checking", "installing", "restarting"].includes(release.status);
    const update = latest && newer(latest, release.current);
    check.disabled = !release.available || updating || posting;
    install.hidden = !update;
    install.disabled = check.disabled;
    const previous = release.versions.filter(version => version !== release.current && !(update && version === latest));
    const oldOptions = Array.from(versions.options, option => option.value);
    if (previous.join("|") !== oldOptions.join("|"))
      versions.replaceChildren(...previous.map(version => new Option(version)));
    history.hidden = !previous.length;
    installed.disabled = check.disabled;
    if (target && target === release.current) {
      status.textContent = "Firmware updated.";
      target = "";
    } else {
      status.textContent = messages[release.status] || (release.available ? "" : "Release updates require a versioned Native Preview.");
    }
  }

  async function refresh() {
    try {
      const response = await fetch(releasesPath, { cache: "no-store", signal: AbortSignal.timeout(2500) });
      if (!response.ok) throw new Error("release_status_unavailable");
      const release: ReleaseStatus = await response.json();
      release.versions = Array.isArray(release.versions) ? release.versions.filter(version => versionPattern.test(version)) : [];
      show(release);
    } catch (_) {
      clearActions();
      status.textContent = target ? "Waiting for the frame to come back…" : "Firmware release information is unavailable.";
    } finally {
      window.clearTimeout(timer);
      if (panel.isConnected) timer = window.setTimeout(refresh, 3000);
    }
  }

  async function request(version: string) {
    if (posting) return;
    posting = true;
    window.clearTimeout(timer);
    clearActions();
    status.textContent = version ? messages.installing : messages.checking;
    try {
      const response = await fetch(releasesPath, {
        method: "POST",
        headers: { "Content-Type": "text/plain" },
        body: version,
        signal: AbortSignal.timeout(4000),
      });
      if (!response.ok) throw new Error("release_request_failed");
      target = version;
      posting = false;
      await refresh();
    } catch (_) {
      posting = false;
      status.textContent = "Could not start the request. Check the frame connection and try again.";
      timer = window.setTimeout(refresh, 3000);
    }
  }

  check.onclick = () => void request("");
  install.onclick = () => void request(latest);
  installed.onclick = () => void request(versions.value);
  clearActions();
  void refresh();
  return panel;
}
