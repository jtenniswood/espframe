  function ensureFavicon() {
    var icon = document.querySelector('link[rel="icon"]') || document.createElement("link");
    icon.rel = "icon";
    icon.type = "image/svg+xml";
    icon.href = "data:image/svg+xml," + encodeURIComponent(FAVICON_SVG);
    if (!icon.parentNode) document.head.appendChild(icon);
  }

  function buildUI() {
    var root = document.createElement("espframe-app");
    root.id = "sp-app";

    var banner = document.createElement("div");
    banner.className = "banner";
    banner.style.display = "none";
    root.appendChild(banner);
    els.banner = banner;

    buildHeader(root);
    buildImmichPage(root);
    buildSettingsPage(root);
    buildLogsPage(root);

    var espApp = document.querySelector("esp-app");
    if (espApp) {
      espApp.parentNode.insertBefore(root, espApp);
    } else {
      document.body.insertBefore(root, document.body.firstChild);
    }
    els.root = root;
    switchTab("immich");
    addSupportButton();
  }

  function addSupportButton() {
    if (document.querySelector(".sp-support-btn")) return;
    var link = document.createElement("a");
    link.className = "sp-support-btn";
    link.href = SUPPORT_URL;
    link.target = "_blank";
    link.rel = "noopener";
    link.setAttribute("aria-label", "Support Espframe");
    link.innerHTML = '<span class="sp-support-icon" aria-hidden="true">&#9749;</span><span>Support Espframe</span>';
    document.body.appendChild(link);
  }

  function buildHeader(parent) {
    var header = document.createElement("div");
    header.className = "sp-header";

    var brand = document.createElement("div");
    brand.className = "sp-brand";
    brand.textContent = "EspFrame";
    header.appendChild(brand);

    var nav = document.createElement("nav");
    nav.className = "sp-nav";
    nav.setAttribute("aria-label", "Primary");

    webUiTabs().forEach(function (t) {
      var tab = document.createElement("div");
      tab.className = "sp-tab";
      tab.setAttribute("role", "tab");
      tab.setAttribute("aria-selected", "false");
      tab.textContent = t.label;
      tab.addEventListener("click", function () { switchTab(t.id); });
      nav.appendChild(tab);
      els["tab_" + t.id] = tab;
    });

    var docsLink = document.createElement("a");
    docsLink.className = "sp-tab sp-tab-docs";
    docsLink.href = DOCS_BASE_URL + "/";
    docsLink.target = "_blank";
    docsLink.rel = "noopener";
    docsLink.innerHTML = 'Docs <span class="sp-docs-icon" aria-hidden="true">&#8599;</span>';
    nav.appendChild(docsLink);

    header.appendChild(nav);
    parent.appendChild(header);
  }

  function webUiTabs() {
    return Array.isArray(WEB_UI_TABS) && WEB_UI_TABS.length
      ? WEB_UI_TABS
      : [{ id: "immich", label: "Immich" }, { id: "settings", label: "Device" }];
  }

  var immichApp;

  function buildImmichPage(parent) {
    var page = document.createElement("div");
    page.id = "sp-immich";
    page.className = "sp-page";

    var wrap = document.createElement("div");
    wrap.className = "sp-settings-wrap";
    page.appendChild(wrap);

    parent.appendChild(page);
    els.immichPage = page;
    immichApp = wrap;
  }

  function buildSettingsPage(parent) {
    var page = document.createElement("div");
    page.id = "sp-settings";
    page.className = "sp-page";

    var wrap = document.createElement("div");
    wrap.className = "sp-settings-wrap";
    page.appendChild(wrap);

    parent.appendChild(page);
    els.settingsPage = page;
    app = wrap;
  }

  function buildLogsPage(parent) {
    var page = document.createElement("div");
    page.id = "sp-logs";
    page.className = "sp-page";

    var toolbar = document.createElement("div");
    toolbar.className = "sp-log-toolbar";
    var clearBtn = document.createElement("button");
    clearBtn.className = "sp-log-clear";
    clearBtn.textContent = "Clear";
    clearBtn.addEventListener("click", function () { els.logOutput.replaceChildren(); });
    toolbar.appendChild(clearBtn);
    page.appendChild(toolbar);

    var output = document.createElement("div");
    output.className = "sp-log-output";
    page.appendChild(output);
    els.logOutput = output;

    parent.appendChild(page);
    els.logsPage = page;
  }

  function switchTab(tab) {
    webUiTabs().forEach(function (tabSpec) {
      var t = tabSpec.id;
      els["tab_" + t].className = "sp-tab" + (tab === t ? " active" : "");
      els["tab_" + t].setAttribute("aria-selected", tab === t ? "true" : "false");
    });
    els.immichPage.className = "sp-page" + (tab === "immich" ? " active" : "");
    els.settingsPage.className = "sp-page" + (tab === "settings" ? " active" : "");
    els.logsPage.className = "sp-page" + (tab === "logs" ? " active" : "");
  }
