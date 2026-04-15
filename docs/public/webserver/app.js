(function () {
  "use strict";

  var TIMEZONES = [
    "Pacific/Midway (GMT-11)","Pacific/Pago_Pago (GMT-11)","Pacific/Honolulu (GMT-10)",
    "America/Adak (GMT-10)","America/Anchorage (GMT-9)","America/Juneau (GMT-9)",
    "America/Los_Angeles (GMT-8)","America/Vancouver (GMT-8)","America/Tijuana (GMT-8)",
    "America/Denver (GMT-7)","America/Phoenix (GMT-7)","America/Edmonton (GMT-7)",
    "America/Boise (GMT-7)","America/Chicago (GMT-6)","America/Mexico_City (GMT-6)",
    "America/Winnipeg (GMT-6)","America/Guatemala (GMT-6)","America/Costa_Rica (GMT-6)",
    "America/New_York (GMT-5)","America/Toronto (GMT-5)","America/Detroit (GMT-5)",
    "America/Havana (GMT-5)","America/Bogota (GMT-5)","America/Lima (GMT-5)",
    "America/Jamaica (GMT-5)","America/Panama (GMT-5)","America/Halifax (GMT-4)",
    "America/Caracas (GMT-4)","America/Santiago (GMT-4)","America/La_Paz (GMT-4)",
    "America/Manaus (GMT-4)","America/Barbados (GMT-4)","America/Puerto_Rico (GMT-4)",
    "America/Santo_Domingo (GMT-4)","America/St_Johns (GMT-3:30)",
    "America/Sao_Paulo (GMT-3)","America/Argentina/Buenos_Aires (GMT-3)",
    "America/Montevideo (GMT-3)","America/Paramaribo (GMT-3)",
    "Atlantic/South_Georgia (GMT-2)","Atlantic/Azores (GMT-1)","Atlantic/Cape_Verde (GMT-1)",
    "UTC (GMT+0)","Europe/London (GMT+0)","Europe/Dublin (GMT+0)","Europe/Lisbon (GMT+0)",
    "Africa/Casablanca (GMT+1)","Africa/Accra (GMT+0)","Atlantic/Reykjavik (GMT+0)",
    "Europe/Paris (GMT+1)","Europe/Berlin (GMT+1)","Europe/Rome (GMT+1)",
    "Europe/Madrid (GMT+1)","Europe/Amsterdam (GMT+1)","Europe/Brussels (GMT+1)",
    "Europe/Vienna (GMT+1)","Europe/Zurich (GMT+1)","Europe/Stockholm (GMT+1)",
    "Europe/Oslo (GMT+1)","Europe/Copenhagen (GMT+1)","Europe/Warsaw (GMT+1)",
    "Europe/Prague (GMT+1)","Europe/Budapest (GMT+1)","Europe/Belgrade (GMT+1)",
    "Africa/Lagos (GMT+1)","Africa/Tunis (GMT+1)","Africa/Cairo (GMT+2)",
    "Europe/Athens (GMT+2)","Europe/Bucharest (GMT+2)","Europe/Helsinki (GMT+2)",
    "Europe/Kyiv (GMT+2)","Europe/Istanbul (GMT+3)","Africa/Johannesburg (GMT+2)",
    "Africa/Nairobi (GMT+3)","Asia/Jerusalem (GMT+2)","Asia/Amman (GMT+3)",
    "Asia/Beirut (GMT+2)","Europe/Moscow (GMT+3)","Asia/Baghdad (GMT+3)",
    "Asia/Riyadh (GMT+3)","Asia/Kuwait (GMT+3)","Asia/Qatar (GMT+3)",
    "Africa/Addis_Ababa (GMT+3)","Asia/Tehran (GMT+3:30)","Asia/Dubai (GMT+4)",
    "Asia/Muscat (GMT+4)","Asia/Baku (GMT+4)","Asia/Tbilisi (GMT+4)",
    "Indian/Mauritius (GMT+4)","Asia/Kabul (GMT+4:30)","Asia/Karachi (GMT+5)",
    "Asia/Tashkent (GMT+5)","Asia/Yekaterinburg (GMT+5)","Asia/Kolkata (GMT+5:30)",
    "Asia/Colombo (GMT+5:30)","Asia/Kathmandu (GMT+5:45)","Asia/Dhaka (GMT+6)",
    "Asia/Almaty (GMT+6)","Asia/Rangoon (GMT+6:30)","Asia/Bangkok (GMT+7)",
    "Asia/Jakarta (GMT+7)","Asia/Ho_Chi_Minh (GMT+7)","Asia/Singapore (GMT+8)",
    "Asia/Kuala_Lumpur (GMT+8)","Asia/Shanghai (GMT+8)","Asia/Hong_Kong (GMT+8)",
    "Asia/Taipei (GMT+8)","Asia/Manila (GMT+8)","Australia/Perth (GMT+8)",
    "Asia/Tokyo (GMT+9)","Asia/Seoul (GMT+9)","Asia/Pyongyang (GMT+9)",
    "Australia/Adelaide (GMT+9:30)","Australia/Darwin (GMT+9:30)",
    "Australia/Sydney (GMT+10)","Australia/Melbourne (GMT+10)","Australia/Brisbane (GMT+10)",
    "Australia/Hobart (GMT+10)","Pacific/Guam (GMT+10)","Pacific/Port_Moresby (GMT+10)",
    "Asia/Vladivostok (GMT+10)","Pacific/Noumea (GMT+11)","Pacific/Norfolk (GMT+11)",
    "Asia/Magadan (GMT+11)","Pacific/Auckland (GMT+12)","Pacific/Fiji (GMT+12)",
    "Pacific/Chatham (GMT+12:45)","Pacific/Tongatapu (GMT+13)","Pacific/Apia (GMT+13)",
    "Pacific/Kiritimati (GMT+14)"
  ];

  var S = {
    clock_options: ["24 Hour", "12 Hour"],
    tz_options: TIMEZONES,
    interval: "15 seconds",
    interval_options: [
      "10 seconds", "15 seconds", "20 seconds", "30 seconds", "45 seconds",
      "1 minute", "2 minutes", "3 minutes", "5 minutes", "10 minutes"
    ],
    conn_timeout: "2 minutes",
    conn_timeout_options: [
      "30 seconds", "45 seconds", "1 minute", "2 minutes", "3 minutes",
      "5 minutes", "10 minutes", "15 minutes", "20 minutes", "30 minutes"
    ],
    brightness: 100,
    backlight_on: true,
    show_clock: true,
    clock_format: "24 Hour",
    immich_url: "",
    api_key: "",
    timezone: "Europe/London (GMT+0)",
    firmware: "",
    installed_version: "",
    latest_version: "",
    update_available: false,
    beta_version: "",
    beta_available: false,
    auto_update: true,
    update_frequency: "Daily",
    update_freq_options: ["Hourly", "Daily", "Weekly", "Monthly"],
    schedule_enabled: false,
    schedule_on_hour: 6,
    schedule_off_hour: 23,
    brightness_current: 0,
    brightness_day: 100,
    brightness_night: 75,
    sunrise: "",
    sunset: "",
    photo_source: "All Photos",
    photo_source_options: ["All Photos", "Favorites", "Album", "Person", "Memories"],
    album_ids: "",
    person_ids: "",
    base_tone_enabled: false,
    base_tone: 0,
    warm_tones_enabled: false,
    warm_tone_intensity: 50,
    warm_tone_override: false,
    portrait_pairing: true,
    display_mode: "Fill",
    display_mode_options: ["Fill", "Fit"],
  };

  var CSS =
    "*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}" +
    ":root{" +
    "--bg:#1b1b1f;--surface:#202127;--surface2:#2e2e32;--border:#3c3f44;" +
    "--border-hover:rgba(255,255,255,.16);--text:#dfdfd6;--text2:#98989f;--text3:#6a6a71;" +
    "--accent:#5c73e7;--accent-hover:#a8b1ff;" +
    "--accent-soft:rgba(100,108,255,.16);--success:#30a46c;--success-soft:rgba(48,164,108,.14);" +
    "--danger:#f14158;--radius:12px;--action-r:9999px;--gap:16px;" +
    "--shadow-1:0 1px 2px rgba(0,0,0,.2),0 1px 2px rgba(0,0,0,.24);" +
    "--shadow-2:0 3px 12px rgba(0,0,0,.28),0 1px 4px rgba(0,0,0,.2);" +
    "--shadow-3:0 12px 32px rgba(0,0,0,.35),0 2px 6px rgba(0,0,0,.24)}" +
    "esp-app{display:none !important}" +
    "html{font-size:16px}" +
    "body{font-family:Inter,ui-sans-serif,-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;" +
    "background:var(--bg);color:var(--text);line-height:1.7;" +
    "min-height:100vh;margin:0;-webkit-font-smoothing:antialiased;-moz-osx-font-smoothing:grayscale}" +
    "#sp-app{max-width:960px;margin:0 auto}" +
    ".sp-header{display:flex;align-items:center;background:var(--bg);" +
    "border-bottom:1px solid var(--border);position:sticky;top:0;z-index:100;height:56px;padding:0 20px}" +
    ".sp-brand{font-size:1rem;font-weight:600;color:var(--text);margin-right:auto;" +
    "white-space:nowrap;letter-spacing:-.01em}" +
    ".sp-nav{display:flex;align-items:center;height:100%}" +
    ".sp-tab{padding:0 16px;height:100%;display:flex;align-items:center;color:var(--text2);cursor:pointer;" +
    "font-size:.875rem;font-weight:500;border-bottom:2px solid transparent;transition:color .2s}" +
    ".sp-tab:hover{color:var(--text)}" +
    ".sp-tab.active{color:var(--accent);border-bottom-color:var(--accent)}" +
    ".sp-page{display:none}.sp-page.active{display:block}" +
    ".sp-settings-wrap{padding:var(--gap)}" +
    ".brand{font-size:1.6rem;font-weight:700;letter-spacing:-.02em;" +
    "background:linear-gradient(135deg,var(--accent) 0%,#a78bfa 100%);" +
    "-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}" +
    "h1{font-size:1.6rem;font-weight:700;margin-bottom:4px;letter-spacing:-.02em}" +
    "h2{font-size:1rem;font-weight:500;margin-bottom:20px;color:var(--text2);letter-spacing:.01em}" +
    ".subtitle{font-size:.9rem;color:var(--text2);margin-bottom:24px;line-height:1.6}" +
    ".card{background:var(--surface);border:1px solid var(--border);" +
    "border-radius:var(--radius);padding:24px;margin-bottom:var(--gap);transition:border-color .25s}" +
    ".card:hover{border-color:#4a4d54}" +
    ".card h3{font-size:.875rem;font-weight:600;margin-bottom:14px;color:var(--text);" +
    "letter-spacing:-.01em}" +
    ".card-header{display:flex;justify-content:space-between;align-items:center;" +
    "cursor:pointer;user-select:none;margin:-24px -24px 0 -24px;padding:24px 24px 0 24px}" +
    ".card-header h3{margin:0}" +
    ".card-body{padding-top:20px}" +
    ".card-chevron{display:inline-flex;align-items:center;justify-content:center;" +
    "width:24px;height:24px;color:var(--text3);transition:transform .25s ease;flex-shrink:0}" +
    ".card-chevron svg{width:100%;height:100%}" +
    ".card.collapsed .card-chevron{transform:rotate(-90deg)}" +
    ".card.collapsed .card-body{display:none}" +
    ".card-header-right{display:flex;align-items:center;gap:8px}" +
    ".on-badge{display:none;align-items:center;gap:4px;" +
    "font-size:.6rem;font-weight:600;color:var(--success);" +
    "padding:2px 8px 2px 6px;background:var(--success-soft);" +
    "border-radius:999px;text-transform:uppercase;letter-spacing:.06em;white-space:nowrap}" +
    ".card.collapsed .on-badge.active{display:inline-flex}" +
    ".on-badge::before{content:'';display:block;width:6px;height:6px;" +
    "border-radius:50%;background:var(--success);flex-shrink:0}" +
    ".field{margin-bottom:22px}.field:last-child{margin-bottom:0}" +
    "label{display:block;font-size:.85rem;color:var(--text2);margin-bottom:6px;font-weight:500}" +
    "input[type='text'],input[type='password'],input[type='url']{" +
    "width:100%;padding:10px 14px;background:var(--surface2);border:1px solid var(--border);" +
    "border-radius:8px;color:var(--text);font-size:.9rem;outline:none;" +
    "transition:border-color .25s,box-shadow .25s;font-family:inherit}" +
    "input[type='text']:focus,input[type='password']:focus,input[type='url']:focus{" +
    "border-color:var(--accent);box-shadow:0 0 0 3px var(--accent-soft)}" +
    "input::placeholder{color:var(--text2);opacity:.7}" +
    ".select,select{width:100%;padding:10px 14px;background:var(--surface2);border:1px solid var(--border);" +
    "border-radius:8px;color:var(--text);font-size:.9rem;outline:none;" +
    "transition:border-color .25s,box-shadow .25s;-webkit-appearance:none;appearance:none;" +
    "font-family:inherit;" +
    "background-image:url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'%3E%3Cpath fill='%23888' d='M6 8L1 3h10z'/%3E%3C/svg%3E\");" +
    "background-repeat:no-repeat;background-position:right 14px center;padding-right:36px}" +
    ".select:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 3px var(--accent-soft)}" +
    "select option{background:var(--surface);color:var(--text)}" +
    ".input-group{display:flex;gap:8px}.input-group input{flex:1}" +
    ".btn{padding:10px 20px;border:none;border-radius:20px;font-size:.875rem;" +
    "font-weight:600;cursor:pointer;transition:background .25s,opacity .25s,box-shadow .25s;" +
    "font-family:inherit;letter-spacing:.01em}" +
    ".btn:active{opacity:.85}" +
    ".btn-primary{background:var(--accent);color:#fff}" +
    ".btn-primary:hover{background:var(--accent-hover);box-shadow:0 2px 12px var(--accent-soft)}" +
    ".btn-secondary{background:var(--surface2);color:var(--text);border:1px solid var(--border)}" +
    ".btn-secondary:hover{border-color:var(--border-hover);background:rgba(255,255,255,.06)}" +
    ".btn-sm{padding:7px 14px;font-size:.8rem}" +
    ".btn-block{width:100%;display:block}" +
    ".btn:disabled{opacity:.35;cursor:not-allowed}" +
    ".field-error{font-size:.75rem;color:var(--danger);margin-top:4px}" +
    ".field-error:empty{display:none}" +
    ".toggle-row{display:flex;justify-content:space-between;align-items:center;min-height:36px}" +
    ".toggle-row span{font-size:.9rem}" +
    ".toggle{position:relative;width:44px;height:24px;" +
    "background:var(--surface2);border-radius:12px;cursor:pointer;" +
    "transition:background .25s;border:1px solid var(--border)}" +
    ".toggle.on{background:var(--accent);border-color:var(--accent)}" +
    ".toggle::after{content:'';position:absolute;top:2px;left:2px;" +
    "width:18px;height:18px;border-radius:50%;background:#fff;" +
    "transition:transform .25s ease;box-shadow:0 1px 3px rgba(0,0,0,.3)}" +
    ".toggle.on::after{transform:translateX(20px)}" +
    ".segment{display:flex;border-radius:8px;overflow:hidden;border:1px solid var(--border)}" +
    ".segment button{flex:1;padding:8px 0;background:var(--surface2);color:var(--text2);" +
    "border:none;font-size:.85rem;cursor:pointer;transition:background .25s,color .25s;font-family:inherit}" +
    ".segment button.active{background:var(--accent);color:#fff}" +
    ".range-wrap{display:flex;align-items:center;gap:12px}" +
    ".range-wrap input[type='range']{flex:1;-webkit-appearance:none;height:4px;" +
    "background:var(--surface2);border-radius:2px;outline:none}" +
    ".range-wrap input[type='range']::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;" +
    "border-radius:50%;background:var(--accent);cursor:pointer;" +
    "box-shadow:0 0 0 3px var(--accent-soft);transition:box-shadow .2s}" +
    ".range-wrap input[type='range']::-webkit-slider-thumb:hover{box-shadow:0 0 0 5px var(--accent-soft)}" +
    ".range-wrap input[type='range']::-moz-range-thumb{width:18px;height:18px;border-radius:50%;" +
    "background:var(--accent);cursor:pointer;border:none}" +
    ".range-val{min-width:42px;text-align:right;font-size:.85rem;color:var(--text2);font-variant-numeric:tabular-nums}" +
    ".range-label{font-size:.85rem;color:var(--text2);white-space:nowrap}" +
    ".status{display:inline-flex;align-items:center;gap:6px;font-size:.8rem;color:var(--text2);margin-top:4px}" +
    ".dot{width:8px;height:8px;border-radius:50%;flex-shrink:0}" +
    ".dot.green{background:var(--success)}.dot.red{background:var(--danger)}.dot.orange{background:#ff9800}" +
    ".wizard-steps{display:flex;gap:8px;margin-bottom:24px}" +
    ".wizard-steps .step{flex:1;height:3px;border-radius:2px;background:var(--surface2);transition:background .3s}" +
    ".wizard-steps .step.active{background:var(--accent)}" +
    ".wizard-steps .step.done{background:var(--success)}" +
    ".wizard-nav{display:flex;gap:8px;margin-top:20px}.wizard-nav .btn{flex:1}" +
    ".fade-in{animation:fadeIn .35s ease}" +
    "@keyframes fadeIn{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:translateY(0)}}" +
    ".sun-info{font-size:.8rem;color:var(--text2);padding:10px 14px;background:var(--surface2);" +
    "border-radius:8px;text-align:center;border:1px solid var(--border)}" +
    ".version{text-align:center;font-size:.75rem;color:var(--text2);margin-top:8px;opacity:.5}" +
    ".fw-body{display:flex;flex-direction:column;gap:12px}" +
    ".fw-body .field{margin-bottom:0}" +
    ".fw-updates{display:flex;flex-direction:column;gap:12px}" +
    ".fw-row{display:flex;align-items:center;justify-content:space-between;min-height:36px}" +
    ".fw-label{font-size:.9rem}.fw-status{font-size:.8rem;color:var(--text2)}" +
    ".field-hint{font-size:.75rem;color:var(--text2);margin-top:6px;margin-bottom:8px}" +
    ".key-mask{flex:1;padding:10px 14px;background:var(--surface2);border:1px solid var(--border);" +
    "border-radius:8px;color:var(--text2);font-size:.9rem;letter-spacing:2px}" +
    ".check-wrap{display:flex;align-items:center;gap:8px;flex-shrink:0}" +
    ".sp-log-toolbar{display:flex;justify-content:flex-end;padding:12px var(--gap) 0}" +
    ".sp-log-clear{background:var(--surface2);color:var(--text);border:1px solid var(--border);" +
    "border-radius:8px;padding:8px 14px;font-size:.8rem;font-weight:500;cursor:pointer;" +
    "font-family:inherit;transition:all .25s}" +
    ".sp-log-clear:hover{background:var(--border);border-color:#4a4d54}" +
    ".sp-log-output{margin:8px var(--gap) var(--gap);padding:16px;background:var(--surface);" +
    "border:1px solid var(--border);border-radius:var(--radius);" +
    "font-family:ui-monospace,'SF Mono',SFMono-Regular,Menlo,Consolas,monospace;" +
    "font-size:.75rem;line-height:1.7;color:var(--text2);overflow-x:auto;overflow-y:auto;" +
    "max-height:70vh;white-space:pre;word-break:break-all}" +
    ".sp-log-line{padding:1px 0;border-left:3px solid transparent;padding-left:8px}" +
    ".sp-log-error{color:#f66f81;border-left-color:#f14158;background:rgba(244,63,94,.08)}" +
    ".sp-log-warn{color:#f9b44e;border-left-color:#da8b17;background:rgba(234,179,8,.06)}" +
    ".sp-log-info{color:#3dd68c}" +
    ".sp-log-config{color:#c8abfa}" +
    ".sp-log-debug{color:#5c73e7}" +
    ".sp-log-verbose{color:var(--text2)}" +
    ".banner{position:fixed;top:16px;left:50%;transform:translateX(-50%);" +
    "z-index:9999;padding:10px 24px;border-radius:var(--radius);" +
    "font-size:.85rem;font-weight:600;color:#fff;" +
    "box-shadow:var(--shadow-2);animation:bannerIn .25s ease;" +
    "max-width:calc(100% - 32px);text-align:center}" +
    ".banner-success{background:var(--success)}.banner-error{background:var(--danger)}" +
    "@keyframes bannerIn{from{opacity:0;transform:translateX(-50%) translateY(-12px)}to{opacity:1;transform:translateX(-50%) translateY(0)}}" +
    ".backup-row{display:flex;gap:8px}.backup-row .btn{flex:1}" +
    ".mb-8{margin-bottom:8px}.mb-12{margin-bottom:12px}.mb-20{margin-bottom:20px}" +
    ".mb-24{margin-bottom:24px}.mt-12{margin-top:12px}";

  var style = document.createElement("style");
  style.textContent = CSS;
  document.head.appendChild(style);

  var fonts = document.createElement("link");
  fonts.rel = "stylesheet";
  fonts.href = "https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap";
  document.head.appendChild(fonts);

  var els = {};
  var app;

  function buildUI() {
    var root = document.createElement("div");
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

    var tabs = [
      { id: "immich", label: "Immich" },
      { id: "settings", label: "Device" },
      { id: "logs", label: "Logs" }
    ];

    tabs.forEach(function (t) {
      var tab = document.createElement("div");
      tab.className = "sp-tab";
      tab.textContent = t.label;
      tab.addEventListener("click", function () { switchTab(t.id); });
      nav.appendChild(tab);
      els["tab_" + t.id] = tab;
    });

    header.appendChild(nav);
    parent.appendChild(header);
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
    clearBtn.addEventListener("click", function () { els.logOutput.innerHTML = ""; });
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
    ["immich", "settings", "logs"].forEach(function (t) {
      els["tab_" + t].className = "sp-tab" + (tab === t ? " active" : "");
    });
    els.immichPage.className = "sp-page" + (tab === "immich" ? " active" : "");
    els.settingsPage.className = "sp-page" + (tab === "settings" ? " active" : "");
    els.logsPage.className = "sp-page" + (tab === "logs" ? " active" : "");
  }

  function eid(domain, name) {
    return "/" + domain + "/" + encodeURIComponent(name);
  }

  var endpoints = {
    immich_url: eid("text", "Connection: Server URL"),
    api_key: eid("text", "Connection: API Key"),
    clock_format: eid("select", "Clock: Format"),
    timezone: eid("select", "Clock: Timezone"),
    interval: eid("select", "Photos: Slideshow Interval"),
    conn_timeout: eid("select", "Screen: Connection Timeout"),
    backlight: eid("light", "Screen: Backlight"),
    show_clock: eid("switch", "Clock: Show"),
    firmware: eid("text_sensor", "Firmware: Version"),
    update: eid("update", "Firmware: Update"),
    update_beta: eid("update", "Firmware: Update Beta"),
    auto_update: eid("switch", "Firmware: Auto Update"),
    update_frequency: eid("select", "Firmware: Update Frequency"),
    schedule_enabled: eid("switch", "Screen: Schedule"),
    schedule_on_hour: eid("number", "Screen: Schedule On"),
    schedule_off_hour: eid("number", "Screen: Schedule Off"),
    brightness_day: eid("number", "Screen: Daytime Brightness"),
    brightness_night: eid("number", "Screen: Nighttime Brightness"),
    sunrise: eid("text_sensor", "Screen: Sunrise"),
    sunset: eid("text_sensor", "Screen: Sunset"),
    photo_source: eid("select", "Photos: Source"),
    album_ids: eid("text", "Photos: Album IDs"),
    person_ids: eid("text", "Photos: Person IDs"),
    base_tone_enabled: eid("switch", "Screen: Tone Adjustment"),
    base_tone: eid("number", "Screen: Display Tone"),
    warm_tones_enabled: eid("switch", "Screen: Night Tone Adjustment"),
    warm_tone_intensity: eid("number", "Screen: Warm Tone Intensity"),
    warm_tone_override: eid("switch", "Screen: Warm Tone Override"),
    portrait_pairing: eid("switch", "Photos: Portrait Pairing"),
    display_mode: eid("select", "Photos: Display Mode"),
  };

  function post(url, params) {
    var fullUrl = params ? url + "?" + new URLSearchParams(params).toString() : url;
    return fetch(fullUrl, { method: "POST" }).then(function (r) {
      if (!r.ok) console.error("POST " + fullUrl + " failed: " + r.status);
      return r;
    }).catch(function (err) {
      console.error("POST " + fullUrl + " error:", err);
      showBanner("Failed to save setting", "error");
    });
  }

  var UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
  function isValidUuidList(str) {
    var s = str.trim();
    if (!s) return true;
    return s.split(",").every(function (id) { return UUID_RE.test(id.trim()); });
  }

  function safeGet(url) {
    return fetch(url)
      .then(function (r) {
        if (!r.ok) return null;
        return r.json();
      })
      .catch(function () {
        return null;
      });
  }

  // --- SSE-based init ---

  var evtSource = null;
  var rendered = false;
  var renderTimer = null;
  var logListenerAttached = false;

  var ANSI_LEVEL = {
    "1;31": "sp-log-error",
    "0;31": "sp-log-error",
    "0;33": "sp-log-warn",
    "0;32": "sp-log-info",
    "0;35": "sp-log-config",
    "0;36": "sp-log-debug",
    "0;37": "sp-log-verbose"
  };
  var ANSI_RE = /\033\[[\d;]*m/g;

  function appendLog(msg, lvl) {
    if (!els.logOutput) return;
    var line = document.createElement("div");
    line.className = "sp-log-line";

    var ansiClass = "";
    var m = msg.match(/\033\[([\d;]+)m/);
    if (m) ansiClass = ANSI_LEVEL[m[1]] || "";

    if (ansiClass) {
      line.classList.add(ansiClass);
    } else if (lvl === 1) line.classList.add("sp-log-error");
    else if (lvl === 2) line.classList.add("sp-log-warn");
    else if (lvl === 3) line.classList.add("sp-log-info");
    else if (lvl === 4) line.classList.add("sp-log-config");
    else if (lvl === 5) line.classList.add("sp-log-debug");
    else if (lvl >= 6) line.classList.add("sp-log-verbose");

    line.textContent = msg.replace(ANSI_RE, "");

    var atBottom = els.logOutput.scrollHeight - els.logOutput.scrollTop - els.logOutput.clientHeight < 40;
    els.logOutput.appendChild(line);
    var overflow = els.logOutput.childNodes.length - 1000;
    if (overflow > 0) {
      for (var i = 0; i < overflow; i++)
        els.logOutput.removeChild(els.logOutput.firstChild);
    }
    if (atBottom) els.logOutput.scrollTop = els.logOutput.scrollHeight;
  }

  // Entity id -> state key mapping; optional optionsKey and default.
  var ENTITY_STATE_MAP = {
    "text/Connection: Server URL": { key: "immich_url" },
    "text/Connection: API Key": { key: "api_key" },
    "select/Clock: Format": { key: "clock_format", optionsKey: "clock_options", default: "24 Hour" },
    "select/Clock: Timezone": { key: "timezone", optionsKey: "tz_options", default: "" },
    "select/Photos: Slideshow Interval": { key: "interval", optionsKey: "interval_options", default: "2 minutes" },
    "select/Screen: Connection Timeout": { key: "conn_timeout", optionsKey: "conn_timeout_options", default: "2 minutes" },
    "switch/Clock: Show": { key: "show_clock", boolFromState: true },
    "text_sensor/Firmware: Version": { key: "firmware" },
    "switch/Firmware: Auto Update": { key: "auto_update", boolFromState: true },
    "select/Firmware: Update Frequency": { key: "update_frequency", optionsKey: "update_freq_options", default: "Daily" },
    "switch/Screen: Schedule": { key: "schedule_enabled", boolFromState: true },
    "number/Screen: Schedule On": { key: "schedule_on_hour", default: 6, number: true },
    "number/Screen: Schedule Off": { key: "schedule_off_hour", default: 23, number: true },
    "number/Screen: Daytime Brightness": { key: "brightness_day", default: 100, number: true },
    "number/Screen: Nighttime Brightness": { key: "brightness_night", default: 75, number: true },
    "text_sensor/Screen: Sunrise": { key: "sunrise" },
    "text_sensor/Screen: Sunset": { key: "sunset" },
    "select/Photos: Source": { key: "photo_source", optionsKey: "photo_source_options", default: "All Photos" },
    "text/Photos: Album IDs": { key: "album_ids" },
    "text/Photos: Person IDs": { key: "person_ids" },
    "switch/Screen: Tone Adjustment": { key: "base_tone_enabled", boolFromState: true },
    "number/Screen: Display Tone": { key: "base_tone", default: 0, number: true },
    "switch/Screen: Night Tone Adjustment": { key: "warm_tones_enabled", boolFromState: true },
    "number/Screen: Warm Tone Intensity": { key: "warm_tone_intensity", default: 50, number: true },
    "switch/Screen: Warm Tone Override": { key: "warm_tone_override", boolFromState: true },
    "switch/Photos: Portrait Pairing": { key: "portrait_pairing", boolFromState: true },
    "select/Photos: Display Mode": { key: "display_mode", optionsKey: "display_mode_options", default: "Fill" }
  };

  function applyEntityToState(d) {
    if (!d || !d.id) return;
    var id = d.id;
    if (id === "light/Screen: Backlight") {
      S.backlight_on = d.state === "ON";
      if (d.brightness != null) {
        S.brightness = Math.round((d.brightness / 255) * 100);
        S.brightness_current = S.brightness;
      }
      return;
    }
    if (id === "update/Firmware: Update") {
      S.installed_version = d.current_version || "";
      S.latest_version = d.latest_version || "";
      S.update_available =
        S.installed_version &&
        S.latest_version &&
        S.installed_version !== S.latest_version;
      return;
    }
    if (id === "update/Firmware: Update Beta") {
      S.beta_version = d.latest_version || "";
      S.beta_available =
        S.beta_version &&
        d.current_version &&
        S.beta_version !== d.current_version;
      return;
    }
    var spec = ENTITY_STATE_MAP[id];
    if (!spec) return;
    var v = d.value != null ? d.value : d.state;
    if (spec.boolFromState) {
      S[spec.key] = v === true || v === "ON";
    } else if (spec.number) {
      S[spec.key] = v != null ? Math.round(Number(v)) : (spec.default !== undefined ? spec.default : 0);
    } else {
      S[spec.key] = v !== undefined && v !== null ? String(v) : (spec.default !== undefined ? spec.default : "");
    }
    if (spec.optionsKey && d.option && d.option.length) S[spec.optionsKey] = d.option;
  }

  function collectState(d) {
    applyEntityToState(d);
  }

  // Single source for settings fetched on load; KEY_TO_ENTITY_ID derived from ENTITY_STATE_MAP.
  var INITIAL_FETCH_KEYS = [
    "photo_source", "album_ids", "person_ids", "interval", "conn_timeout",
    "schedule_enabled", "schedule_on_hour", "schedule_off_hour",
    "sunrise", "sunset",
    "base_tone_enabled", "base_tone", "warm_tones_enabled", "warm_tone_intensity", "warm_tone_override",
    "portrait_pairing",
    "display_mode"
  ];
  function getEntityIdForStateKey(key) {
    for (var id in ENTITY_STATE_MAP) {
      if (ENTITY_STATE_MAP[id].key === key) return id;
    }
    return null;
  }
  var KEY_TO_ENTITY_ID = {};
  INITIAL_FETCH_KEYS.forEach(function (k) {
    var id = getEntityIdForStateKey(k);
    if (id) KEY_TO_ENTITY_ID[k] = id;
  });

  function fetchDeviceSettingsState() {
    var urls = INITIAL_FETCH_KEYS.map(function (k) { return safeGet(endpoints[k]); });
    return Promise.all(urls).then(function (res) {
      for (var i = 0; i < res.length; i++) {
        var data = res[i];
        if (!data) continue;
        applyEntityToState({
          id: KEY_TO_ENTITY_ID[INITIAL_FETCH_KEYS[i]],
          value: data.value,
          state: data.state,
          option: data.option
        });
      }
    });
  }

  function tryRender() {
    if (rendered) return;
    rendered = true;
    if (S.immich_url) {
      fetchDeviceSettingsState().then(renderSettings);
      return;
    }
    Promise.all([
      safeGet(endpoints.immich_url),
      safeGet(endpoints.api_key)
    ]).then(function (res) {
      if (res[0]) S.immich_url = res[0].value || res[0].state || "";
      if (res[1]) S.api_key = res[1].value || res[1].state || "";
      if (S.immich_url) {
        fetchDeviceSettingsState().then(renderSettings);
      } else {
        renderWizard();
      }
    });
  }

  function initSSE() {
    try {
      evtSource = new EventSource("/events");

      evtSource.addEventListener("state", function (e) {
        try {
          var d = JSON.parse(e.data);
          collectState(d);
          if (rendered) handleLiveEvent(d);
        } catch (_) {}

        if (!rendered) {
          clearTimeout(renderTimer);
          renderTimer = setTimeout(tryRender, 500);
        }
      });

      if (!logListenerAttached) {
        logListenerAttached = true;
        evtSource.addEventListener("log", function (e) {
          var d;
          try { d = JSON.parse(e.data); } catch (_) { d = { msg: e.data }; }
          appendLog(d.msg || e.data, d.lvl);
        });
      }

      evtSource.onerror = function () {
        if (!rendered) {
          clearTimeout(renderTimer);
          renderTimer = setTimeout(tryRender, 1000);
        }
      };

      evtSource.onopen = function () {};
    } catch (_) {
      tryRender();
    }

    setTimeout(function () {
      if (!rendered) tryRender();
    }, 5000);
  }

  // --- Wizard ---

  function renderWizard() {
    var step = 1;
    app.innerHTML = "";
    var wrap = el("div", "fade-in");
    wrap.innerHTML =
      '<p class="subtitle">Let\'s connect your photo frame</p>';
    var steps = el("div", "wizard-steps");
    var s1 = el("div", "step active");
    var s2 = el("div", "step");
    steps.appendChild(s1);
    steps.appendChild(s2);
    wrap.appendChild(steps);

    var body = el("div");
    wrap.appendChild(body);
    app.appendChild(wrap);

    function showStep() {
      body.innerHTML = "";
      if (step === 1) {
        s1.className = "step active";
        s2.className = "step";
        body.appendChild(renderStep1());
      } else {
        s1.className = "step done";
        s2.className = "step active";
        body.appendChild(renderStep2());
      }
    }

    function renderStep1() {
      var card = el("div", "card fade-in");
      card.innerHTML = "<h3>Connection</h3>";

      var f1 = field("Immich Server URL");
      var urlInput = input("url", S.immich_url, "http://192.168.0.1:2283");
      f1.appendChild(urlInput);
      card.appendChild(f1);

      var f2 = field("API Key");
      var grp = el("div", "input-group");
      var keyInput = input("password", S.api_key, "Your Immich API key");
      var showBtn = el("button", "btn btn-secondary");
      showBtn.textContent = "Show";
      showBtn.type = "button";
      showBtn.onclick = function () {
        var isPass = keyInput.type === "password";
        keyInput.type = isPass ? "text" : "password";
        showBtn.textContent = isPass ? "Hide" : "Show";
      };
      grp.appendChild(keyInput);
      grp.appendChild(showBtn);
      f2.appendChild(grp);
      card.appendChild(f2);

      var nav = el("div", "wizard-nav");
      var nextBtn = el("button", "btn btn-primary");
      nextBtn.textContent = "Connect";
      nextBtn.onclick = function () {
        var u = urlInput.value.trim();
        var k = keyInput.value.trim();
        if (!u || !k) return;
        nextBtn.disabled = true;
        nextBtn.textContent = "Saving\u2026";
        post(endpoints.immich_url + "/set", { value: u })
          .then(function () {
            return new Promise(function (r) { setTimeout(r, 500); });
          })
          .then(function () {
            return post(endpoints.api_key + "/set", { value: k });
          })
          .then(function () {
            S.immich_url = u;
            S.api_key = k;
            step = 2;
            showStep();
          });
      };
      nav.appendChild(nextBtn);
      card.appendChild(nav);
      return card;
    }

    function renderStep2() {
      var card = el("div", "card fade-in");
      card.innerHTML = "<h3>Clock & Timezone</h3>";

      var f1 = field("Clock Format");
      f1.appendChild(
        selectFromOptions(S.clock_options, S.clock_format, function (v) {
          S.clock_format = v;
          post(endpoints.clock_format + "/set", { option: v });
        })
      );
      card.appendChild(f1);

      var f2 = field("Timezone");
      f2.appendChild(
        timezoneSelect(S.tz_options, S.timezone, function (v) {
          post(endpoints.timezone + "/set", { option: v });
          S.timezone = v;
        })
      );
      card.appendChild(f2);

      var nav = el("div", "wizard-nav");
      var backBtn = el("button", "btn btn-secondary");
      backBtn.textContent = "Back";
      backBtn.onclick = function () {
        step = 1;
        showStep();
      };
      var doneBtn = el("button", "btn btn-primary");
      doneBtn.textContent = "Done";
      doneBtn.onclick = function () {
        renderSettings();
      };
      nav.appendChild(backBtn);
      nav.appendChild(doneBtn);
      card.appendChild(nav);
      return card;
    }

    showStep();
  }

  // --- Settings ---

  function renderSettings() {
    app.innerHTML = "";
    immichApp.innerHTML = "";
    var immichWrap = el("div", "fade-in");
    var wrap = el("div", "fade-in");

    // Connection
    var connBody = el("div");
    var connStatus = el("div", "status mb-12");
    connStatus.id = "conn-status";

    function showSaved(msg) {
      connStatus.innerHTML = '<span class="dot green"></span> ' + (msg || "Saved");
      clearTimeout(connStatus._t);
      connStatus._t = setTimeout(function () {
        connStatus.textContent = "";
      }, 3000);
    }

    var f1 = field("Immich Server URL");
    var urlInput = input("url", S.immich_url, "http://192.168.0.1:2283");
    urlInput.onchange = function () {
      post(endpoints.immich_url + "/set", { value: urlInput.value.trim() });
      showSaved("URL saved");
    };
    f1.appendChild(urlInput);
    connBody.appendChild(f1);

    var f2 = field("API Key");
    var keyConfigured = S.api_key && S.api_key.length > 0;
    var keyWrap = el("div");

    function showKeyMasked() {
      keyWrap.innerHTML = "";
      var row = el("div", "input-group");
      var mask = el("div");
      mask.className = "key-mask";
      mask.textContent = "\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022";
      var cb = el("button", "btn btn-secondary");
      cb.textContent = "Change";
      cb.type = "button";
      cb.onclick = function () {
        keyWrap.innerHTML = "";
        keyWrap.appendChild(makeKeyInput());
      };
      row.appendChild(mask);
      row.appendChild(cb);
      keyWrap.appendChild(row);
    }

    function makeKeyInput() {
      var grp = el("div", "input-group");
      var keyInput = input("text", "", "Paste your Immich API key");
      var saveBtn = el("button", "btn btn-primary");
      saveBtn.textContent = "Save";
      saveBtn.type = "button";
      saveBtn.onclick = function () {
        var v = keyInput.value.trim();
        if (!v) return;
        saveBtn.disabled = true;
        saveBtn.textContent = "Saving\u2026";
        post(endpoints.api_key + "/set", { value: v }).then(function () {
          showSaved("API key saved");
          showKeyMasked();
        });
      };
      grp.appendChild(keyInput);
      grp.appendChild(saveBtn);
      return grp;
    }

    if (keyConfigured) {
      showKeyMasked();
    } else {
      keyWrap.appendChild(makeKeyInput());
    }

    f2.appendChild(keyWrap);
    connBody.appendChild(f2);

    connBody.appendChild(connStatus);
    immichWrap.appendChild(makeCollapsibleCard("Connection", connBody, true));

    // Photo Source
    var srcBody = el("div");
    var fSrc = field("Source");
    var srcSel = selectFromOptions(S.photo_source_options, S.photo_source, function (v) {
      S.photo_source = v;
      albumField.style.display = v === "Album" ? "" : "none";
      personField.style.display = v === "Person" ? "" : "none";
    });

    var albumField = field("Album IDs");
    var albumInput = input("text", S.album_ids, "Paste album IDs, comma-separated");
    var albumError = el("div", "field-error");
    var albumHint = el("div");
    albumHint.className = "field-hint";
    albumHint.textContent = "Find IDs in your Immich server URL bar";
    albumField.appendChild(albumInput);
    albumField.appendChild(albumError);
    albumField.appendChild(albumHint);
    albumField.style.display = S.photo_source === "Album" ? "" : "none";

    var personField = field("Person IDs");
    var personInput = input("text", S.person_ids, "Paste person IDs, comma-separated");
    var personError = el("div", "field-error");
    var personHint = el("div");
    personHint.className = "field-hint";
    personHint.textContent = "Find IDs in your Immich server URL bar";
    personField.appendChild(personInput);
    personField.appendChild(personError);
    personField.appendChild(personHint);
    personField.style.display = S.photo_source === "Person" ? "" : "none";

    var applyBtn = el("button", "btn btn-primary btn-block mt-12");
    applyBtn.textContent = "Apply";
    applyBtn.onclick = function () {
      albumError.textContent = "";
      personError.textContent = "";
      var src_val = srcSel.value;
      if (src_val === "Album" && !isValidUuidList(albumInput.value)) {
        albumError.textContent = "Invalid UUID format";
        return;
      }
      if (src_val === "Person" && !isValidUuidList(personInput.value)) {
        personError.textContent = "Invalid UUID format";
        return;
      }
      applyBtn.disabled = true;
      applyBtn.textContent = "Applying\u2026";
      post(endpoints.photo_source + "/set", { option: src_val });
      post(endpoints.album_ids + "/set", { value: albumInput.value.trim() });
      post(endpoints.person_ids + "/set", { value: personInput.value.trim() });
      post(eid("button", "Apply Photo Source") + "/press").then(function () {
        applyBtn.textContent = "Applied";
        setTimeout(function () {
          applyBtn.disabled = false;
          applyBtn.textContent = "Apply";
        }, 2000);
      });
    };

    fSrc.appendChild(srcSel);
    srcBody.appendChild(fSrc);
    srcBody.appendChild(albumField);
    srcBody.appendChild(personField);
    srcBody.appendChild(applyBtn);

    immichWrap.appendChild(makeCollapsibleCard("Photo Source", srcBody, true));

    // Photo Settings
    var photoBody = el("div");

    var fPairToggle = field("");
    var pairTr = el("div", "toggle-row");
    pairTr.innerHTML = "<span>Portrait Pairing</span>";
    var pairTog = el("div", S.portrait_pairing ? "toggle on" : "toggle");
    pairTog.onclick = function () {
      S.portrait_pairing = !S.portrait_pairing;
      pairTog.className = S.portrait_pairing ? "toggle on" : "toggle";
      post(endpoints.portrait_pairing + (S.portrait_pairing ? "/turn_on" : "/turn_off"));
    };
    pairTr.appendChild(pairTog);
    fPairToggle.appendChild(pairTr);
    photoBody.appendChild(fPairToggle);

    var fDisplayMode = field("Display Mode");
    fDisplayMode.appendChild(
      selectFromOptions(S.display_mode_options, S.display_mode, function (v) {
        S.display_mode = v;
        post(endpoints.display_mode + "/set", { option: v });
      })
    );
    photoBody.appendChild(fDisplayMode);

    immichWrap.appendChild(makeCollapsibleCard("Photo Settings", photoBody, true));

    // Frequency
    var dispBody = el("div");
    var f3 = field("Slideshow Interval");
    f3.appendChild(
      selectFromOptions(S.interval_options, S.interval, function (v) {
        S.interval = v;
        post(endpoints.interval + "/set", { option: v });
      })
    );
    dispBody.appendChild(f3);
    var f4 = field("Connection Timeout");
    f4.appendChild(
      selectFromOptions(S.conn_timeout_options, S.conn_timeout, function (v) {
        S.conn_timeout = v;
        post(endpoints.conn_timeout + "/set", { option: v });
      })
    );
    dispBody.appendChild(f4);
    immichWrap.appendChild(makeCollapsibleCard("Frequency", dispBody, true));

    immichApp.appendChild(immichWrap);

    // Screen Brightness
    var dnDetails = el("div");

    var fDayBrt = field("Daytime Brightness");
    var rwDay = el("div", "range-wrap");
    var daySlider = document.createElement("input");
    daySlider.type = "range";
    daySlider.min = 10;
    daySlider.max = 100;
    daySlider.step = 5;
    daySlider.value = S.brightness_day;
    var dayVal = el("span", "range-val");
    dayVal.textContent = Math.round(S.brightness_day) + "%";
    daySlider.oninput = function () {
      dayVal.textContent = daySlider.value + "%";
    };
    daySlider.onchange = function () {
      post(endpoints.brightness_day + "/set", { value: daySlider.value });
    };
    rwDay.appendChild(daySlider);
    rwDay.appendChild(dayVal);
    fDayBrt.appendChild(rwDay);
    dnDetails.appendChild(fDayBrt);

    var fNightBrt = field("Nighttime Brightness");
    var rwNight = el("div", "range-wrap");
    var nightSlider = document.createElement("input");
    nightSlider.type = "range";
    nightSlider.min = 10;
    nightSlider.max = 100;
    nightSlider.step = 5;
    nightSlider.value = S.brightness_night;
    var nightVal = el("span", "range-val");
    nightVal.textContent = Math.round(S.brightness_night) + "%";
    nightSlider.oninput = function () {
      nightVal.textContent = nightSlider.value + "%";
    };
    nightSlider.onchange = function () {
      post(endpoints.brightness_night + "/set", { value: nightSlider.value });
    };
    rwNight.appendChild(nightSlider);
    rwNight.appendChild(nightVal);
    fNightBrt.appendChild(rwNight);
    dnDetails.appendChild(fNightBrt);

    var fSunInfo = el("div", "field sun-info");
    fSunInfo.id = "sun-info";
    function updateSunInfo() {
      updateSunInfoElement(fSunInfo);
    }
    updateSunInfo();
    dnDetails.appendChild(fSunInfo);

    wrap.appendChild(makeCollapsibleCard("Screen Brightness", dnDetails, true));

    // Screen Tone
    var toneBadge = makeBadge(S.base_tone_enabled || S.warm_tones_enabled);
    var warmBody = el("div");

    var fBaseToneToggle = field("");
    var baseTr = el("div", "toggle-row");
    baseTr.innerHTML = "<span>Screen Tone Adjustment</span>";
    var baseTog = el("div", S.base_tone_enabled ? "toggle on" : "toggle");
    var baseDetails = el("div");
    baseDetails.style.display = S.base_tone_enabled ? "" : "none";

    baseTog.onclick = function () {
      S.base_tone_enabled = !S.base_tone_enabled;
      baseTog.className = S.base_tone_enabled ? "toggle on" : "toggle";
      baseDetails.style.display = S.base_tone_enabled ? "" : "none";
      toneBadge.className = "on-badge" + ((S.base_tone_enabled || S.warm_tones_enabled) ? " active" : "");
      post(endpoints.base_tone_enabled + (S.base_tone_enabled ? "/turn_on" : "/turn_off"));
    };
    baseTr.appendChild(baseTog);
    fBaseToneToggle.appendChild(baseTr);
    fBaseToneToggle.style.marginBottom = "8px";
    warmBody.appendChild(fBaseToneToggle);

    var fBaseTone = field("");
    var rwBase = el("div", "range-wrap");
    var baseLabelL = el("span", "range-label");
    baseLabelL.textContent = "Cooler";
    var baseSlider = document.createElement("input");
    baseSlider.type = "range";
    baseSlider.min = 0;
    baseSlider.max = 100;
    baseSlider.step = 5;
    baseSlider.value = S.base_tone;
    baseSlider.onchange = function () {
      post(endpoints.base_tone + "/set", { value: baseSlider.value });
    };
    var baseLabelR = el("span", "range-label");
    baseLabelR.textContent = "Warmer";
    rwBase.appendChild(baseLabelL);
    rwBase.appendChild(baseSlider);
    rwBase.appendChild(baseLabelR);
    fBaseTone.appendChild(rwBase);
    baseDetails.appendChild(fBaseTone);
    baseDetails.style.marginBottom = "28px";
    warmBody.appendChild(baseDetails);

    var fWarmToggle = field("");
    var warmTr = el("div", "toggle-row");
    warmTr.innerHTML = "<span>Night Tone Adjustment</span>";
    var warmTog = el("div", S.warm_tones_enabled ? "toggle on" : "toggle");
    var nightDetails = el("div");
    nightDetails.style.display = S.warm_tones_enabled ? "" : "none";

    warmTog.onclick = function () {
      S.warm_tones_enabled = !S.warm_tones_enabled;
      warmTog.className = S.warm_tones_enabled ? "toggle on" : "toggle";
      nightDetails.style.display = S.warm_tones_enabled ? "" : "none";
      toneBadge.className = "on-badge" + ((S.base_tone_enabled || S.warm_tones_enabled) ? " active" : "");
      post(endpoints.warm_tones_enabled + (S.warm_tones_enabled ? "/turn_on" : "/turn_off"));
    };
    warmTr.appendChild(warmTog);
    fWarmToggle.appendChild(warmTr);
    fWarmToggle.style.marginBottom = "8px";
    warmBody.appendChild(fWarmToggle);

    var fWarmInt = field("");
    var rwWarm = el("div", "range-wrap");
    var warmLabelL = el("span", "range-label");
    warmLabelL.textContent = "Cooler";
    var warmSlider = document.createElement("input");
    warmSlider.type = "range";
    warmSlider.min = 10;
    warmSlider.max = 100;
    warmSlider.step = 5;
    warmSlider.value = S.warm_tone_intensity;
    warmSlider.onchange = function () {
      post(endpoints.warm_tone_intensity + "/set", { value: warmSlider.value });
    };
    var warmLabelR = el("span", "range-label");
    warmLabelR.textContent = "Warmer";
    rwWarm.appendChild(warmLabelL);
    rwWarm.appendChild(warmSlider);
    rwWarm.appendChild(warmLabelR);
    fWarmInt.appendChild(rwWarm);
    nightDetails.appendChild(fWarmInt);

    var fOverride = field("");
    var overTr = el("div", "toggle-row");
    overTr.innerHTML = "<span>Turn on until sunrise</span>";
    var overTog = el("div", S.warm_tone_override ? "toggle on" : "toggle");
    overTog.onclick = function () {
      S.warm_tone_override = !S.warm_tone_override;
      overTog.className = S.warm_tone_override ? "toggle on" : "toggle";
      post(endpoints.warm_tone_override + (S.warm_tone_override ? "/turn_on" : "/turn_off"));
    };
    overTr.appendChild(overTog);
    fOverride.appendChild(overTr);
    nightDetails.appendChild(fOverride);

    warmBody.appendChild(nightDetails);
    wrap.appendChild(makeCollapsibleCard("Screen Tone", warmBody, true, toneBadge));

    // Schedule
    var schedBadge = makeBadge(S.schedule_enabled);
    var schedBody = el("div");
    var fSchedToggle = field("");
    var schedTr = el("div", "toggle-row");
    schedTr.innerHTML = "<span>Enable Schedule</span>";
    var schedTog = el("div", S.schedule_enabled ? "toggle on" : "toggle");
    var schedDetails = el("div");
    schedDetails.style.display = S.schedule_enabled ? "" : "none";

    schedTog.onclick = function () {
      S.schedule_enabled = !S.schedule_enabled;
      schedTog.className = S.schedule_enabled ? "toggle on" : "toggle";
      schedDetails.style.display = S.schedule_enabled ? "" : "none";
      schedBadge.className = "on-badge" + (S.schedule_enabled ? " active" : "");
      post(endpoints.schedule_enabled + (S.schedule_enabled ? "/turn_on" : "/turn_off"));
    };
    schedTr.appendChild(schedTog);
    fSchedToggle.appendChild(schedTr);
    schedBody.appendChild(fSchedToggle);

    var fOnTime = field("On Time");
    var onSel = document.createElement("select");
    onSel.className = "select";
    for (var h = 0; h < 24; h++) {
      var o = document.createElement("option");
      o.value = h;
      o.textContent = formatHour(h);
      if (h === Math.round(S.schedule_on_hour)) o.selected = true;
      onSel.appendChild(o);
    }
    onSel.onchange = function () {
      S.schedule_on_hour = parseInt(onSel.value);
      post(endpoints.schedule_on_hour + "/set", { value: onSel.value });
    };
    fOnTime.appendChild(onSel);
    schedDetails.appendChild(fOnTime);

    var fOffTime = field("Off Time");
    var offSel = document.createElement("select");
    offSel.className = "select";
    for (var h2 = 0; h2 < 24; h2++) {
      var o2 = document.createElement("option");
      o2.value = h2;
      o2.textContent = formatHour(h2);
      if (h2 === Math.round(S.schedule_off_hour)) o2.selected = true;
      offSel.appendChild(o2);
    }
    offSel.onchange = function () {
      S.schedule_off_hour = parseInt(offSel.value);
      post(endpoints.schedule_off_hour + "/set", { value: offSel.value });
    };
    fOffTime.appendChild(offSel);
    schedDetails.appendChild(fOffTime);

    schedBody.appendChild(schedDetails);
    wrap.appendChild(makeCollapsibleCard("Screen Schedule", schedBody, true, schedBadge));

    // Clock
    var clockBadge = makeBadge(S.show_clock);
    var clkBody = el("div");
    var f5 = field("");
    var tr = el("div", "toggle-row");
    tr.innerHTML = "<span>Show Clock</span>";
    var tog = el("div", S.show_clock ? "toggle on" : "toggle");
    tog.onclick = function () {
      S.show_clock = !S.show_clock;
      tog.className = S.show_clock ? "toggle on" : "toggle";
      clockBadge.className = "on-badge" + (S.show_clock ? " active" : "");
      post(
        endpoints.show_clock + (S.show_clock ? "/turn_on" : "/turn_off")
      );
    };
    tr.appendChild(tog);
    f5.appendChild(tr);
    clkBody.appendChild(f5);

    var f6 = field("Format");
    f6.appendChild(
      selectFromOptions(S.clock_options, S.clock_format, function (v) {
        S.clock_format = v;
        post(endpoints.clock_format + "/set", { option: v });
      })
    );
    clkBody.appendChild(f6);

    var f7 = field("Timezone");
    f7.appendChild(
      timezoneSelect(S.tz_options, S.timezone, function (v) {
        post(endpoints.timezone + "/set", { option: v });
        S.timezone = v;
      })
    );
    clkBody.appendChild(f7);
    wrap.appendChild(makeCollapsibleCard("Clock", clkBody, true, clockBadge));

    // Firmware
    var fwBody = el("div", "fw-body");
    var versionRow = el("div", "field fw-row");
    var versionLabel = el("span", "fw-label");
    versionLabel.innerHTML = '<span style="color:var(--text2)">Installed</span> ' +
      esc(S.firmware || S.installed_version || "Dev");
    var checkBtn = el("button", "btn btn-secondary btn-sm");
    checkBtn.textContent = "Check for Update";
    var statusMsg = el("span", "fw-status");
    versionRow.appendChild(versionLabel);
    var checkWrap = el("div");
    checkWrap.className = "check-wrap";
    checkWrap.appendChild(statusMsg);
    checkWrap.appendChild(checkBtn);
    versionRow.appendChild(checkWrap);
    var versionBlock = el("div");
    versionBlock.appendChild(versionRow);
    fwBody.appendChild(versionBlock);

    var updatesSection = el("div", "fw-updates");
    var updateRow = el("div");
    updatesSection.appendChild(updateRow);
    var betaRow = el("div");
    updatesSection.appendChild(betaRow);
    fwBody.appendChild(updatesSection);

    function renderUpdateRow() {
      updateRow.innerHTML = "";
      if (!S.update_available) return;
      var row = el("div", "field fw-row");
      var label = el("span", "fw-label");
      label.innerHTML = '<span style="color:var(--text2)">Stable</span> ' + esc(S.latest_version);
      var installBtn = el("button", "btn btn-primary btn-sm");
      installBtn.textContent = "Install";
      installBtn.onclick = function () {
        installBtn.disabled = true;
        installBtn.textContent = "Installing\u2026";
        post(endpoints.update + "/install");
      };
      row.appendChild(label);
      row.appendChild(installBtn);
      updateRow.appendChild(row);
    }

    function renderBetaRow() {
      betaRow.innerHTML = "";
      if (!S.beta_available) return;
      var row = el("div", "field fw-row");
      var label = el("span", "fw-label");
      label.innerHTML = '<span style="color:var(--text2)">Pre-release</span> ' + esc(S.beta_version);
      var betaBtn = el("button", "btn btn-secondary btn-sm");
      betaBtn.textContent = "Install";
      betaBtn.onclick = function () {
        betaBtn.disabled = true;
        betaBtn.textContent = "Installing\u2026";
        post(endpoints.update_beta + "/install");
      };
      row.appendChild(label);
      row.appendChild(betaBtn);
      betaRow.appendChild(row);
    }

    renderUpdateRow();
    renderBetaRow();

    checkBtn.onclick = function () {
      checkBtn.disabled = true;
      checkBtn.textContent = "Checking\u2026";
      statusMsg.textContent = "";
      post(eid("button", "Firmware: Check for Update") + "/press")
        .then(function () {
          return new Promise(function (r) {
            setTimeout(r, 4000);
          });
        })
        .then(function () {
          return safeGet(endpoints.update);
        })
        .then(function (data) {
          checkBtn.disabled = false;
          checkBtn.textContent = "Check for Update";
          var hasUpdate = data && data.value &&
            (data.current_version
              ? data.current_version !== data.latest_version
              : data.state === "UPDATE AVAILABLE");
          if (hasUpdate) {
            S.update_available = true;
            S.latest_version = data.latest_version || data.value;
            renderUpdateRow();
          }
          return safeGet(endpoints.update_beta);
        })
        .then(function (betaData) {
          if (betaData && (betaData.latest_version || betaData.value)) {
            S.beta_version = betaData.latest_version || betaData.value;
            S.beta_available = betaData.current_version
              ? betaData.latest_version !== betaData.current_version
              : betaData.state === "UPDATE AVAILABLE";
          }
          renderBetaRow();
          if (!S.update_available && !S.beta_available) {
            statusMsg.textContent = "Up to date";
            statusMsg.style.color = "var(--success)";
          }
        });
    };

    var autoUpdateOptions = ["Disabled"].concat(S.update_freq_options);
    var currentAutoUpdate = S.auto_update ? S.update_frequency : "Disabled";
    var freqField = field("Auto updates");
    freqField.appendChild(
      selectFromOptions(autoUpdateOptions, currentAutoUpdate, function (v) {
        if (v === "Disabled") {
          S.auto_update = false;
          post(endpoints.auto_update + "/turn_off");
        } else {
          S.auto_update = true;
          S.update_frequency = v;
          post(endpoints.auto_update + "/turn_on");
          post(endpoints.update_frequency + "/set", { option: v });
        }
      })
    );
    fwBody.appendChild(freqField);

    wrap.appendChild(makeCollapsibleCard("Firmware", fwBody, true));

    // Backup
    var backupBody = el("div");
    var backupRow = el("div", "backup-row");
    var exportBtn = el("button", "btn btn-secondary");
    exportBtn.innerHTML = "Export";
    exportBtn.onclick = exportConfig;
    var importBtn = el("button", "btn btn-secondary");
    importBtn.innerHTML = "Import";
    importBtn.onclick = importConfig;
    backupRow.appendChild(exportBtn);
    backupRow.appendChild(importBtn);
    backupBody.appendChild(backupRow);
    wrap.appendChild(makeCollapsibleCard("Backup", backupBody, true));

    app.appendChild(wrap);

    if (S.firmware) {
      var verLine = el("div", "version");
      verLine.textContent = "v" + S.firmware;
      app.appendChild(verLine);
    }
  }

  // --- SSE live updates (after render) ---

  function handleLiveEvent(d) {
    if (!d || !d.id) return;
    var id = d.id;
    if (id === "light/Screen: Backlight") {
      S.backlight_on = d.state === "ON";
      if (d.brightness != null) {
        S.brightness = Math.round((d.brightness / 255) * 100);
        S.brightness_current = S.brightness;
      }
    } else if (id === "switch/Clock: Show") {
      S.show_clock = d.state === "ON" || d.value === true;
    } else if (id === "text_sensor/Screen: Sunrise") {
      S.sunrise = d.value || d.state || "";
      updateSunInfoElement(document.getElementById("sun-info"));
    } else if (id === "text_sensor/Screen: Sunset") {
      S.sunset = d.value || d.state || "";
      updateSunInfoElement(document.getElementById("sun-info"));
    }
  }

  function updateSunInfoElement(el) {
    if (!el) return;
    if (!S.sunrise && !S.sunset) {
      el.style.display = "none";
      return;
    }
    el.style.display = "";
    var t = "";
    if (S.sunrise) t += "Sunrise: " + esc(S.sunrise);
    if (S.sunrise && S.sunset) t += " \u00a0/\u00a0 ";
    if (S.sunset) t += "Sunset: " + esc(S.sunset);
    el.innerHTML = t;
  }

  // --- Hour formatting ---

  function formatHour(h) {
    h = Math.round(h);
    if (h === 0) return "12:00 AM";
    if (h < 12) return h + ":00 AM";
    if (h === 12) return "12:00 PM";
    return (h - 12) + ":00 PM";
  }

  // --- Select helpers ---

  function selectFromOptions(options, current, onChange, optionDisplayFn) {
    var display = optionDisplayFn || function (o) { return o; };
    var sel = document.createElement("select");
    sel.className = "select";
    options.forEach(function (o) {
      var opt = document.createElement("option");
      opt.value = o;
      opt.textContent = display(o);
      if (o === current) opt.selected = true;
      sel.appendChild(opt);
    });
    sel.onchange = function () {
      onChange(sel.value);
    };
    return sel;
  }

  function timezoneSelect(options, current, onChange) {
    return selectFromOptions(options, current, onChange, function (o) {
      return o.replace(/_/g, " ");
    });
  }

  // --- Helpers ---

  function el(tag, cls) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    return e;
  }

  function makeBadge(isActive) {
    var badge = el("span", "on-badge" + (isActive ? " active" : ""));
    badge.textContent = "On";
    return badge;
  }

  function makeCollapsibleCard(title, bodyElement, defaultCollapsed, badgeEl) {
    var card = el("div", "card");
    var header = el("div", "card-header");
    var h3 = document.createElement("h3");
    h3.textContent = title;
    var rightWrap = el("div", "card-header-right");
    if (badgeEl) rightWrap.appendChild(badgeEl);
    var chevron = el("span", "card-chevron");
    chevron.innerHTML = "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M6 9l6 6 6-6\"/></svg>";
    rightWrap.appendChild(chevron);
    header.appendChild(h3);
    header.appendChild(rightWrap);
    var body = el("div", "card-body");
    body.appendChild(bodyElement);
    card.appendChild(header);
    card.appendChild(body);
    if (defaultCollapsed) card.classList.add("collapsed");
    header.onclick = function () { card.classList.toggle("collapsed"); };
    return card;
  }

  function field(labelText) {
    var f = el("div", "field");
    if (labelText) {
      var l = document.createElement("label");
      l.textContent = labelText;
      f.appendChild(l);
    }
    return f;
  }

  function input(type, value, placeholder) {
    var i = document.createElement("input");
    i.type = type;
    i.value = value || "";
    if (placeholder) i.placeholder = placeholder;
    return i;
  }

  function esc(s) {
    var d = document.createElement("div");
    d.textContent = s;
    return d.innerHTML;
  }

  // --- Banner ---

  var bannerTimer = null;
  function showBanner(msg, type) {
    if (!els.banner) return;
    els.banner.textContent = msg;
    els.banner.className = "banner banner-" + (type || "success");
    els.banner.style.display = "";
    clearTimeout(bannerTimer);
    bannerTimer = setTimeout(function () { els.banner.style.display = "none"; }, 5000);
  }

  // --- Import / Export ---

  function exportConfig() {
    var data = {
      version: 1,
      exported_at: new Date().toISOString(),
      connection: {
        immich_url: S.immich_url,
        api_key: S.api_key
      },
      photos: {
        source: S.photo_source,
        album_ids: S.album_ids,
        person_ids: S.person_ids,
        portrait_pairing: S.portrait_pairing,
        display_mode: S.display_mode
      },
      frequency: {
        interval: S.interval,
        conn_timeout: S.conn_timeout
      },
      clock: {
        show: S.show_clock,
        format: S.clock_format,
        timezone: S.timezone
      },
      screen: {
        brightness_day: S.brightness_day,
        brightness_night: S.brightness_night,
        schedule_enabled: S.schedule_enabled,
        schedule_on_hour: S.schedule_on_hour,
        schedule_off_hour: S.schedule_off_hour,
        base_tone_enabled: S.base_tone_enabled,
        base_tone: S.base_tone,
        warm_tones_enabled: S.warm_tones_enabled,
        warm_tone_intensity: S.warm_tone_intensity,
        warm_tone_override: S.warm_tone_override
      }
    };

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
        var clk = data.clock || {};
        var scr = data.screen || {};

        if (c.immich_url !== undefined) {
          S.immich_url = c.immich_url;
          post(endpoints.immich_url + "/set", { value: c.immich_url });
        }
        if (c.api_key !== undefined) {
          S.api_key = c.api_key;
          post(endpoints.api_key + "/set", { value: c.api_key });
        }

        if (p.source !== undefined) {
          S.photo_source = p.source;
          post(endpoints.photo_source + "/set", { option: p.source });
        }
        if (p.album_ids !== undefined) {
          if (!isValidUuidList(p.album_ids)) {
            showBanner("Import skipped invalid album IDs", "error");
          } else {
            S.album_ids = p.album_ids;
            post(endpoints.album_ids + "/set", { value: p.album_ids });
          }
        }
        if (p.person_ids !== undefined) {
          if (!isValidUuidList(p.person_ids)) {
            showBanner("Import skipped invalid person IDs", "error");
          } else {
            S.person_ids = p.person_ids;
            post(endpoints.person_ids + "/set", { value: p.person_ids });
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

        if (f.interval !== undefined) {
          S.interval = f.interval;
          post(endpoints.interval + "/set", { option: f.interval });
        }
        if (f.conn_timeout !== undefined) {
          S.conn_timeout = f.conn_timeout;
          post(endpoints.conn_timeout + "/set", { option: f.conn_timeout });
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
          S.timezone = clk.timezone;
          post(endpoints.timezone + "/set", { option: clk.timezone });
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

        showBanner("Settings imported successfully", "success");
        renderSettings();
      };
      reader.readAsText(fileInput.files[0]);
    });

    document.body.appendChild(fileInput);
    fileInput.click();
    document.body.removeChild(fileInput);
  }

  // --- Init ---

  buildUI();
  initSSE();
})();
