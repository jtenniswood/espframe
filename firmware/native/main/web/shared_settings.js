// ESPFRAME: generated from typed docs/webserver/src and product/contract; run `npm run generate` to update.
(() => {
  // docs/webserver/src/api_client.ts
  var f = Object.defineProperty;
  var p = (u2, e, t) => e in u2 ? f(u2, e, { enumerable: true, configurable: true, writable: true, value: t }) : u2[e] = t;
  var s = (u2, e, t) => p(u2, typeof e != "symbol" ? e + "" : e, t);
  var EspframeApiError = class extends Error {
    constructor(t, n, i, r, a) {
      super(n);
      s(this, "kind", t);
      s(this, "status", i);
      s(this, "code", r);
      s(this, "field", a);
      this.name = "EspframeApiError";
    }
  };
  function c(u2) {
    return typeof u2 == "object" && u2 !== null && !Array.isArray(u2);
  }
  var EspframeApiClient = class {
    constructor(e, t = 5e3) {
      s(this, "capabilitiesPath", e);
      s(this, "timeoutMs", t);
      s(this, "queue", Promise.resolve());
      s(this, "capabilities", null);
      s(this, "negotiated");
      s(this, "resetEpoch");
      s(this, "resetAccepted", false);
    }
    waitForWrites() {
      return this.queue;
    }
    enqueue(e) {
      const t = this.queue.then(e);
      return this.queue = t.catch(() => {
      }), t;
    }
    error(e, t, n) {
      const i = c(n) ? n : {}, r = e === 404 || e === 405 ? "unavailable" : e === 409 ? "conflict" : e === 400 || e === 422 ? "validation" : "server";
      return new EspframeApiError(
        r,
        typeof i.error == "string" ? i.error : t,
        e,
        typeof i.error == "string" ? i.error : void 0,
        typeof i.field == "string" ? i.field : void 0
      );
    }
    async requestWith(e, t, n) {
      const i = new AbortController(), r = setTimeout(() => i.abort(), this.timeoutMs);
      try {
        const a = await fetch(e, { ...t, signal: i.signal });
        return await n(a);
      } catch (a) {
        throw a instanceof EspframeApiError ? a : a && typeof a == "object" && a.name === "AbortError" ? new EspframeApiError("timeout", "request_timeout") : new EspframeApiError("offline", "device_offline");
      } finally {
        clearTimeout(r);
      }
    }
    request(e, t = {}) {
      return this.requestWith(e, t, async (n) => n);
    }
    requestJson(e, t, n) {
      return this.requestWith(e, t, (i) => this.json(i, n));
    }
    async json(e, t) {
      let n = null;
      try {
        n = await e.json();
      } catch (i) {
        throw i && typeof i == "object" && i.name === "AbortError" ? new EspframeApiError("timeout", "request_timeout") : e.ok ? new EspframeApiError("server", "invalid_server_response", e.status) : this.error(e.status, t);
      }
      if (!e.ok) throw this.error(e.status, t, n);
      return n;
    }
    parseCapabilities(e) {
      return !c(e) || typeof e.contract_version != "number" || typeof e.api_version != "number" || typeof e.configuration_path != "string" || e.update_mode !== "atomic" || typeof e.configuration_available != "boolean" || typeof e.configuration_read != "boolean" || typeof e.configuration_write != "boolean" || typeof e.legacy_entity_api != "boolean" ? null : e;
    }
    negotiate() {
      return this.negotiated !== void 0 ? Promise.resolve(this.negotiated) : (this.capabilities || (this.capabilities = this.requestJson(this.capabilitiesPath, { cache: "no-store" }, "capabilities_unavailable").then((e) => {
        const t = this.parseCapabilities(e);
        return t ? (this.negotiated = t.configuration_available && t.configuration_read && t.configuration_write ? t : null, this.negotiated) : (this.negotiated = null, null);
      }).catch((e) => {
        if (e instanceof EspframeApiError && e.kind === "unavailable")
          return this.negotiated = null, null;
        throw this.capabilities = null, e;
      })), this.capabilities);
    }
    async getConfigurationSnapshot() {
      const e = await this.pollConfiguration();
      if (!e) throw new EspframeApiError("server", "invalid_configuration_snapshot");
      return this.useConfigurationSnapshot(e.snapshot), e.snapshot;
    }
    useConfigurationSnapshot(e) {
      this.resetEpoch = e.reset_epoch;
    }
    async pollConfiguration(e) {
      const t = await this.negotiate();
      if (!t) throw new EspframeApiError("unavailable", "configuration_api_unavailable");
      const n = await this.requestWith(t.configuration_path, {
        cache: "no-store",
        headers: e ? { "If-None-Match": e } : void 0
      }, async (i) => {
        var r;
        return e && i.status === 304 ? null : {
          payload: await this.json(i, "configuration_api_failed"),
          etag: ((r = i.headers) == null ? void 0 : r.get("ETag")) || void 0
        };
      });
      return n ? { snapshot: this.parseConfiguration(n.payload, t), etag: n.etag } : null;
    }
    parseConfiguration(e, t) {
      if (!c(e) || e.api_version !== t.api_version || t.reset_path && !Number.isInteger(e.reset_epoch) || !c(e.values) || !Array.isArray(e.unavailable) || !e.unavailable.every((a) => typeof a == "string"))
        throw new EspframeApiError("server", "invalid_configuration_snapshot");
      let n = typeof e.api_key_configured == "boolean" ? e.api_key_configured : void 0;
      const i = {};
      for (const [a, o] of Object.entries(e.values)) {
        if (a === "api_key") {
          if (n !== void 0) throw new EspframeApiError("server", "invalid_configuration_snapshot");
          n = !!o;
          continue;
        }
        if (typeof o != "string" && typeof o != "number" && typeof o != "boolean")
          throw new EspframeApiError("server", "invalid_configuration_snapshot");
        i[a] = o;
      }
      n === void 0 && (n = false);
      let r;
      if (e.secrets_configured !== void 0) {
        if (!c(e.secrets_configured)) throw new EspframeApiError("server", "invalid_configuration_snapshot");
        r = {};
        for (const [a, o] of Object.entries(e.secrets_configured)) {
          if (typeof o != "boolean") throw new EspframeApiError("server", "invalid_configuration_snapshot");
          r[a] = o;
        }
      }
      return {
        api_version: t.api_version,
        reset_epoch: Number.isInteger(e.reset_epoch) ? e.reset_epoch : 0,
        api_key_configured: n,
        secrets_configured: r,
        values: i,
        unavailable: e.unavailable
      };
    }
    async legacyPost(e, t) {
      return this.requestWith(e, t === void 0 ? { method: "POST" } : {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: t
      }, async (n) => {
        if (!n.ok) {
          let i = null;
          try {
            i = await n.json();
          } catch (r) {
            if (r && typeof r == "object" && r.name === "AbortError")
              throw new EspframeApiError("timeout", "request_timeout");
          }
          throw this.error(n.status, "legacy_write_failed", i);
        }
        return n;
      });
    }
    async legacyWrite(e) {
      if (e.domain === "switch") {
        await this.legacyPost(e.url + (e.value ? "/turn_on" : "/turn_off"));
        return;
      }
      const t = new URLSearchParams({ [e.domain === "select" ? "option" : "value"]: String(e.value) }).toString();
      await this.legacyPost(e.url + "/set", t);
    }
    updateSettings(e, t, n = e) {
      return this.enqueue(async () => {
        if (this.resetAccepted) throw new EspframeApiError("conflict", "reset_pending", 409, "reset_pending");
        const i = await this.negotiate();
        if (!i) {
          for (const o of t) await this.legacyWrite(o);
          return null;
        }
        this.resetEpoch === void 0 && await this.getConfigurationSnapshot();
        const r = i.firmware_rotation_policy === true ? e : n, a = new URLSearchParams({ [i.configuration_parameter || "configuration"]: JSON.stringify({ api_version: i.api_version, reset_epoch: this.resetEpoch, values: r }) }).toString();
        try {
          const o = await this.requestJson(i.configuration_path, {
            method: "POST",
            headers: { "Content-Type": i.configuration_encoding || "application/x-www-form-urlencoded" },
            body: a
          }, "configuration_update_failed");
          if (!c(o) || o.status !== "accepted") throw new EspframeApiError("server", "configuration_update_failed");
          return await new Promise((l) => setTimeout(l, 100)), o;
        } catch (o) {
          if (!i.legacy_entity_api || !(o instanceof EspframeApiError) || o.kind !== "unavailable") throw o;
          this.negotiated = null;
          for (const l of t) await this.legacyWrite(l);
          return null;
        }
      });
    }
    reset(e) {
      return this.enqueue(async () => {
        if (this.resetAccepted) return;
        const t = await this.negotiate();
        if (!t || !t.reset_path)
          throw new EspframeApiError("unavailable", "reset_unavailable");
        await this.legacyPost(t.reset_path, new URLSearchParams({ mode: e }).toString()), this.resetAccepted = true;
      });
    }
    post(e, t) {
      if (this.resetAccepted) return Promise.reject(new EspframeApiError("conflict", "reset_pending", 409, "reset_pending"));
      const n = t ? "?" + new URLSearchParams(Object.entries(t).map(([i, r]) => [i, String(r)])).toString() : "";
      return this.enqueue(() => this.legacyPost(e + n));
    }
    postText(e, t, n = false) {
      return this.resetAccepted ? Promise.reject(new EspframeApiError("conflict", "reset_pending", 409, "reset_pending")) : this.enqueue(() => {
        const i = new URLSearchParams({ value: t }).toString(), r = n && (e + "?" + i).length <= 120 ? "?" + i : "";
        return this.legacyPost(e + r, i);
      });
    }
    getJson(e) {
      return this.requestJson(e, { cache: "no-store" }, "legacy_read_failed");
    }
  };

  // docs/webserver/src/shared_controls.ts
  function selectFromOptions(e, n, t, r) {
    var i = r || function(a) {
      return a;
    }, o = document.createElement("select");
    return o.className = "select", e.forEach(function(a) {
      var d = document.createElement("option");
      d.value = a, d.textContent = i(a), a === n && (d.selected = true), o.appendChild(d);
    }), o.onchange = function() {
      t(o.value);
    }, o;
  }
  function el(e, n) {
    var t = document.createElement(e);
    return n && (t.className = n), t;
  }
  function setStatus(e, n, t, r) {
    if (e) {
      if (e.replaceChildren(), !n) {
        e.textContent = "";
        return;
      }
      var i = el("span", "dot " + (t || "green"));
      e.appendChild(i), e.appendChild(document.createTextNode(" " + n)), clearTimeout(e._t), r && (e._t = setTimeout(function() {
        e.textContent = "";
      }, r));
    }
  }
  function button(e, n, t) {
    var r = el("button", n || "btn btn-secondary");
    return r.type = "button", r.textContent = e, t && (r.onclick = t), r;
  }
  var u = 0;
  function bindDisclosure(e, n, t, r, i) {
    t.id || (t.id = "disclosure-" + ++u), e.setAttribute("aria-controls", t.id);
    function o() {
      e.setAttribute("aria-expanded", String(n.classList.contains(r) === i));
    }
    o(), e.onclick = function(a) {
      a.stopPropagation(), n.classList.toggle(r), o();
    };
  }
  function connectFieldLabels(e) {
    e.querySelectorAll(".field > label").forEach(function(n) {
      var t = n.parentElement.querySelector("input,select,textarea");
      t && (t.id || (t.id = "setting-" + ++u), n.htmlFor = t.id);
    });
  }
  function makeCollapsibleCard(e, n, t, r) {
    var i = el("div", "card"), o = el("div", "card-header"), a = document.createElement("h3"), d = el("button", "card-toggle");
    d.type = "button", d.textContent = e, a.appendChild(d);
    var l = el("div", "card-header-right");
    r && l.appendChild(r);
    var p2 = el("span", "card-chevron");
    p2.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M6 9l6 6 6-6"/></svg>', l.appendChild(p2), o.appendChild(a), o.appendChild(l);
    var c2 = el("div", "card-body");
    return c2.appendChild(n), i.appendChild(o), i.appendChild(c2), t && i.classList.add("collapsed"), bindDisclosure(d, i, c2, "collapsed", false), o.onclick = function() {
      d.click();
    }, i;
  }
  function field(e) {
    var n = el("div", "field");
    if (e) {
      var t = document.createElement("label");
      t.textContent = e, n.appendChild(t);
    }
    return n;
  }

  // docs/webserver/src/native_firmware.ts
  var T = "/api/firmware/releases";
  var S = /^v\d+\.\d+\.\d+-preview\.\d+$/;
  var A = {
    checking: "Checking for updates\u2026",
    unavailable: "Could not check for Native Preview releases.",
    installing: "Downloading and installing firmware\u2026",
    restarting: "Waiting for the frame to restart\u2026",
    failed: "The frame could not install that release."
  };
  function j(s2, c2) {
    if (!S.test(c2)) return false;
    const b = (e) => e.match(/\d+/g).map(Number), v = b(c2);
    for (const [e, a] of b(s2).entries())
      if (a !== v[e]) return a > v[e];
    return false;
  }
  function nativeFirmwarePanel() {
    const s2 = el("div", "native-firmware"), c2 = (t) => {
      const n = el("div", "fw-row"), i = el("span", "fw-label");
      return i.textContent = "\u2014", n.append(t, i), s2.append(n), i;
    }, b = c2("Current"), v = c2("Available"), e = el("p", "fw-status");
    e.setAttribute("role", "status");
    const a = button("Check for Update", "btn btn-secondary btn-sm"), d = button("Install Update", "btn btn-primary btn-sm"), h = button("Install version", "btn btn-secondary btn-sm"), P = el("div", "fw-actions");
    P.append(a, d), s2.append(P);
    const w = field("Previous versions"), u2 = el("select", "select");
    u2.setAttribute("aria-label", "Previous versions"), w.append(u2, h), s2.append(w, e);
    let r = "", f2 = false, m, p2 = "";
    function C() {
      a.disabled = true, d.hidden = true, w.hidden = true;
    }
    function _(t) {
      b.textContent = t.current || "Unknown", r = t.versions[0] || "", v.textContent = r || "\u2014";
      const n = ["checking", "installing", "restarting"].includes(t.status), i = r && j(r, t.current);
      a.disabled = !t.available || n || f2, d.hidden = !i, d.disabled = a.disabled;
      const y = t.versions.filter((o) => o !== t.current && !(i && o === r)), R = Array.from(u2.options, (o) => o.value);
      y.join("|") !== R.join("|") && u2.replaceChildren(...y.map((o) => new Option(o))), w.hidden = !y.length, h.disabled = a.disabled, p2 && p2 === t.current ? (e.textContent = "Firmware updated.", p2 = "") : e.textContent = A[t.status] || (t.available ? "" : "Release updates require a versioned Native Preview.");
    }
    async function g() {
      try {
        const t = await fetch(T, { cache: "no-store", signal: AbortSignal.timeout(2500) });
        if (!t.ok) throw new Error("release_status_unavailable");
        const n = await t.json();
        n.versions = Array.isArray(n.versions) ? n.versions.filter((i) => S.test(i)) : [], _(n);
      } catch (t) {
        C(), e.textContent = p2 ? "Waiting for the frame to come back\u2026" : "Firmware release information is unavailable.";
      } finally {
        window.clearTimeout(m), s2.isConnected && (m = window.setTimeout(g, 3e3));
      }
    }
    async function k(t) {
      if (!f2) {
        f2 = true, window.clearTimeout(m), C(), e.textContent = t ? A.installing : A.checking;
        try {
          if (!(await fetch(T, {
            method: "POST",
            headers: { "Content-Type": "text/plain" },
            body: t,
            signal: AbortSignal.timeout(4e3)
          })).ok) throw new Error("release_request_failed");
          p2 = t, f2 = false, await g();
        } catch (n) {
          f2 = false, e.textContent = "Could not start the request. Check the frame connection and try again.", m = window.setTimeout(g, 3e3);
        }
      }
    }
    return a.onclick = () => {
      k("");
    }, d.onclick = () => {
      k(r);
    }, h.onclick = () => {
      k(u2.value);
    }, C(), g(), s2;
  }

  // docs/webserver/src/app.ts
  var PRODUCT_SETTINGS = { "photo_source": { "entity": "select/Photos: Source", "domain": "select", "default": "All Photos", "options": ["All Photos", "Favorites", "Album", "Person", "Tag", "Memories", "Custom"] }, "memories_window": { "entity": "select/Photos: Memories Window", "domain": "select", "default": "Within 2 Days", "options": ["Same Day", "Within 1 Day", "Within 2 Days", "Within 3 Days", "Within 7 Days"] }, "memories_fallback": { "entity": "switch/Photos: Memories Fallback", "domain": "switch", "default": true, "options": [] }, "album_order": { "entity": "select/Photos: Album Order", "domain": "select", "default": "Random albums", "options": ["Random albums", "Album list order"] }, "tag_matching": { "entity": "select/Photos: Tag Matching", "domain": "select", "default": "Any selected tag", "options": ["Any selected tag", "All selected tags"] }, "date_filter_mode": { "entity": "select/Photos: Date Filter Mode", "domain": "select", "default": "Fixed Range", "options": ["Fixed Range", "Relative Range"] }, "relative_unit": { "entity": "select/Photos: Relative Unit", "domain": "select", "default": "Years", "options": ["Months", "Years"] }, "photo_orientation": { "entity": "select/Photos: Orientation", "domain": "select", "default": "Any", "options": ["Any", "Portrait Only", "Landscape Only"] }, "display_mode": { "entity": "select/Photos: Display Mode", "domain": "select", "default": "Fill", "options": ["Fill", "Fit"] }, "transition": { "entity": "select/Photos: Transition", "domain": "select", "default": "Cross Fade", "options": ["None", "Cross Fade", "Fade to Black", "Fade to White", "Slide Left", "Slide Right", "Slide Up", "Slide Down", "Random"] }, "transition_speed": { "entity": "select/Photos: Transition Speed", "domain": "select", "default": "Fast", "options": ["Fast", "Normal", "Slow"] }, "interval": { "entity": "select/Photos: Slideshow Interval", "domain": "select", "default": "15 seconds", "options": ["10 seconds", "15 seconds", "20 seconds", "30 seconds", "45 seconds", "1 minute", "2 minutes", "3 minutes", "5 minutes", "10 minutes", "15 minutes", "30 minutes", "1 hour", "2 hours", "4 hours", "8 hours", "16 hours", "24 hours"] }, "slideshow_progress_ring_enabled": { "entity": "switch/Photos: Slideshow Progress Ring", "domain": "switch", "default": false, "options": [] }, "conn_timeout": { "entity": "select/Screen: Connection Timeout", "domain": "select", "default": "10 minutes", "options": ["30 seconds", "45 seconds", "1 minute", "2 minutes", "3 minutes", "5 minutes", "10 minutes", "15 minutes", "20 minutes", "30 minutes"] }, "screen_rotation": { "entity": "select/Screen: Rotation", "domain": "select", "default": "0", "options": ["0", "180"], "developerOptions": ["90", "270"] }, "photo_metadata_date_format": { "entity": "select/Device: Metadata Date Format", "domain": "select", "default": "Date Taken", "options": ["Relative Date", "Date Taken"] }, "photo_metadata_date_taken_format": { "entity": "select/Device: Metadata Date Taken Format", "domain": "select", "default": "1 January, 2026", "options": ["1 January, 2026", "January 1, 2026"] }, "clock_format": { "entity": "select/Clock: Format", "domain": "select", "default": "24 Hour", "options": ["24 Hour", "12 Hour"] }, "update_frequency": { "entity": "select/Firmware: Update Frequency", "domain": "select", "default": "Daily", "options": ["Hourly", "Daily", "Weekly", "Monthly"] }, "auto_update": { "entity": "switch/Firmware: Auto Update", "domain": "switch", "default": true, "options": [] }, "c6_auto_update": { "entity": "switch/WiFi Firmware: Auto Update", "domain": "switch", "default": true, "options": [] }, "date_filter_enabled": { "entity": "switch/Photos: Date Filter", "domain": "switch", "default": false, "options": [] }, "date_from": { "entity": "text/Photos: Date From", "domain": "text", "default": "", "options": [], "maxLength": 10 }, "date_to": { "entity": "text/Photos: Date To", "domain": "text", "default": "", "options": [], "maxLength": 10 }, "relative_amount": { "entity": "number/Photos: Relative Amount", "domain": "number", "default": 1, "options": [], "min": 1, "max": 120, "step": 1 }, "schedule_enabled": { "entity": "switch/Screen: Schedule Enabled", "domain": "switch", "default": false, "options": [] }, "schedule_on_hour": { "entity": "number/Screen: Schedule On Hour", "domain": "number", "default": 6, "options": [], "min": 0, "max": 23, "step": 1 }, "schedule_off_hour": { "entity": "number/Screen: Schedule Off Hour", "domain": "number", "default": 23, "options": [], "min": 0, "max": 23, "step": 1 }, "schedule_wake_timeout": { "entity": "number/Screen: Schedule Wake Timeout", "domain": "number", "default": 60, "options": [], "min": 10, "max": 3600, "step": 10 }, "brightness_day": { "entity": "number/Screen: Daytime Brightness", "domain": "number", "default": 100, "options": [], "min": 10, "max": 100, "step": 5 }, "brightness_night": { "entity": "number/Screen: Nighttime Brightness", "domain": "number", "default": 75, "options": [], "min": 10, "max": 100, "step": 5 }, "base_tone_enabled": { "entity": "switch/Screen: Tone Adjustment", "domain": "switch", "default": false, "options": [] }, "base_tone": { "entity": "number/Screen: Display Tone", "domain": "number", "default": 0, "options": [], "min": 0, "max": 100, "step": 5 }, "warm_tones_enabled": { "entity": "switch/Screen: Night Tone Adjustment", "domain": "switch", "default": false, "options": [] }, "warm_tone_intensity": { "entity": "number/Screen: Warm Tone Intensity", "domain": "number", "default": 50, "options": [], "min": 10, "max": 100, "step": 5 }, "warm_tone_override": { "entity": "switch/Screen: Warm Tone Override", "domain": "switch", "default": false, "options": [] }, "portrait_pairing": { "entity": "switch/Photos: Portrait Pairing", "domain": "switch", "default": true, "options": [] }, "portrait_pairing_range": { "entity": "select/Photos: Portrait Pairing Range", "domain": "select", "default": "Same Day", "options": ["Same Day", "Within 1 Day", "Within 2 Days", "Within 3 Days", "Within 5 Days", "Within 7 Days", "Within 14 Days", "Within 30 Days", "Any Time"] }, "portrait_pairs_only": { "entity": "switch/Photos: Paired Portraits Only", "domain": "switch", "default": false, "options": [] }, "photo_metadata_date_enabled": { "entity": "switch/Device: Metadata Date", "domain": "switch", "default": true, "options": [] }, "photo_metadata_location_enabled": { "entity": "switch/Device: Metadata Location", "domain": "switch", "default": true, "options": [] }, "albums_enabled": { "entity": "switch/Photos: Albums Enabled", "domain": "switch", "default": false, "options": [] }, "people_enabled": { "entity": "switch/Photos: People Enabled", "domain": "switch", "default": false, "options": [] }, "tags_enabled": { "entity": "switch/Photos: Tags Enabled", "domain": "switch", "default": false, "options": [] }, "favorites_enabled": { "entity": "switch/Photos: Favorites Enabled", "domain": "switch", "default": false, "options": [] }, "rating_enabled": { "entity": "switch/Photos: Rating Enabled", "domain": "switch", "default": false, "options": [] }, "location_enabled": { "entity": "switch/Photos: Location Enabled", "domain": "switch", "default": false, "options": [] }, "inclusion_matching": { "entity": "select/Photos: Inclusion Groups", "domain": "select", "default": "Match all enabled groups", "options": ["Match all enabled groups", "Match any enabled group"] }, "album_matching": { "entity": "select/Photos: Album Matching", "domain": "select", "default": "Any selected album", "options": ["Any selected album", "All selected albums"] }, "person_matching": { "entity": "select/Photos: Person Matching", "domain": "select", "default": "Any selected person", "options": ["Any selected person", "All selected people"] }, "favorite_mode": { "entity": "select/Photos: Favorites", "domain": "select", "default": "Any", "options": ["Any", "Favorites only", "Exclude favorites"] }, "minimum_rating": { "entity": "select/Photos: Minimum Rating", "domain": "select", "default": "Any", "options": ["Any", "1+", "2+", "3+", "4+", "5+"] }, "filter_country": { "entity": "text/Photos: Country", "domain": "text", "default": "", "options": [], "maxLength": 96 }, "filter_state": { "entity": "text/Photos: State or Province", "domain": "text", "default": "", "options": [], "maxLength": 96 }, "filter_city": { "entity": "text/Photos: City", "domain": "text", "default": "", "options": [], "maxLength": 96 }, "device_language": { "entity": "select/Device: Language", "domain": "select", "default": "en", "options": ["en", "cs", "da", "de", "es", "fi", "fr", "he", "hu", "it", "nb", "nl", "pl", "pt", "pt-br", "ro", "ru", "sk", "sl", "sv", "tr", "uk"] } };
  var TIMEZONES = ["Pacific/Midway (GMT-11)", "Pacific/Pago_Pago (GMT-11)", "Pacific/Honolulu (GMT-10)", "America/Adak (GMT-10)", "America/Anchorage (GMT-9)", "America/Juneau (GMT-9)", "America/Los_Angeles (GMT-8)", "America/Vancouver (GMT-8)", "America/Tijuana (GMT-8)", "America/Denver (GMT-7)", "America/Phoenix (GMT-7)", "America/Edmonton (GMT-7)", "America/Boise (GMT-7)", "America/Chicago (GMT-6)", "America/Mexico_City (GMT-6)", "America/Winnipeg (GMT-6)", "America/Guatemala (GMT-6)", "America/Costa_Rica (GMT-6)", "America/New_York (GMT-5)", "America/Toronto (GMT-5)", "America/Detroit (GMT-5)", "America/Havana (GMT-5)", "America/Bogota (GMT-5)", "America/Lima (GMT-5)", "America/Jamaica (GMT-5)", "America/Panama (GMT-5)", "America/Halifax (GMT-4)", "America/Caracas (GMT-4)", "America/Santiago (GMT-4)", "America/La_Paz (GMT-4)", "America/Manaus (GMT-4)", "America/Barbados (GMT-4)", "America/Puerto_Rico (GMT-4)", "America/Santo_Domingo (GMT-4)", "America/St_Johns (GMT-3:30)", "America/Sao_Paulo (GMT-3)", "America/Argentina/Buenos_Aires (GMT-3)", "America/Montevideo (GMT-3)", "America/Paramaribo (GMT-3)", "Atlantic/South_Georgia (GMT-2)", "Atlantic/Azores (GMT-1)", "Atlantic/Cape_Verde (GMT-1)", "UTC (GMT+0)", "Europe/London (GMT+0)", "Europe/Dublin (GMT+0)", "Europe/Lisbon (GMT+0)", "Africa/Casablanca (GMT+1)", "Africa/Accra (GMT+0)", "Atlantic/Reykjavik (GMT+0)", "Europe/Paris (GMT+1)", "Europe/Berlin (GMT+1)", "Europe/Rome (GMT+1)", "Europe/Madrid (GMT+1)", "Europe/Amsterdam (GMT+1)", "Europe/Brussels (GMT+1)", "Europe/Vienna (GMT+1)", "Europe/Zurich (GMT+1)", "Europe/Stockholm (GMT+1)", "Europe/Oslo (GMT+1)", "Europe/Copenhagen (GMT+1)", "Europe/Warsaw (GMT+1)", "Europe/Prague (GMT+1)", "Europe/Budapest (GMT+1)", "Europe/Belgrade (GMT+1)", "Africa/Lagos (GMT+1)", "Africa/Tunis (GMT+1)", "Africa/Cairo (GMT+2)", "Europe/Athens (GMT+2)", "Europe/Bucharest (GMT+2)", "Europe/Helsinki (GMT+2)", "Europe/Kyiv (GMT+2)", "Europe/Istanbul (GMT+3)", "Africa/Johannesburg (GMT+2)", "Africa/Nairobi (GMT+3)", "Asia/Jerusalem (GMT+2)", "Asia/Amman (GMT+3)", "Asia/Beirut (GMT+2)", "Europe/Moscow (GMT+3)", "Asia/Baghdad (GMT+3)", "Asia/Riyadh (GMT+3)", "Asia/Kuwait (GMT+3)", "Asia/Qatar (GMT+3)", "Africa/Addis_Ababa (GMT+3)", "Asia/Tehran (GMT+3:30)", "Asia/Dubai (GMT+4)", "Asia/Muscat (GMT+4)", "Asia/Baku (GMT+4)", "Asia/Tbilisi (GMT+4)", "Indian/Mauritius (GMT+4)", "Asia/Kabul (GMT+4:30)", "Asia/Karachi (GMT+5)", "Asia/Tashkent (GMT+5)", "Asia/Yekaterinburg (GMT+5)", "Asia/Kolkata (GMT+5:30)", "Asia/Colombo (GMT+5:30)", "Asia/Kathmandu (GMT+5:45)", "Asia/Dhaka (GMT+6)", "Asia/Almaty (GMT+5)", "Asia/Rangoon (GMT+6:30)", "Asia/Bangkok (GMT+7)", "Asia/Jakarta (GMT+7)", "Asia/Ho_Chi_Minh (GMT+7)", "Asia/Singapore (GMT+8)", "Asia/Kuala_Lumpur (GMT+8)", "Asia/Shanghai (GMT+8)", "Asia/Hong_Kong (GMT+8)", "Asia/Taipei (GMT+8)", "Asia/Manila (GMT+8)", "Australia/Perth (GMT+8)", "Asia/Tokyo (GMT+9)", "Asia/Seoul (GMT+9)", "Asia/Pyongyang (GMT+9)", "Australia/Adelaide (GMT+9:30)", "Australia/Darwin (GMT+9:30)", "Australia/Sydney (GMT+10)", "Australia/Melbourne (GMT+10)", "Australia/Brisbane (GMT+10)", "Australia/Hobart (GMT+10)", "Pacific/Guam (GMT+10)", "Pacific/Port_Moresby (GMT+10)", "Asia/Vladivostok (GMT+10)", "Pacific/Noumea (GMT+11)", "Pacific/Norfolk (GMT+11)", "Asia/Magadan (GMT+11)", "Pacific/Auckland (GMT+12)", "Pacific/Fiji (GMT+12)", "Pacific/Chatham (GMT+12:45)", "Pacific/Tongatapu (GMT+13)", "Pacific/Apia (GMT+13)", "Pacific/Kiritimati (GMT+14)"];
  var TIMEZONE_LABELS = { "Pacific/Midway (GMT-11)": "Pacific/Midway (GMT-11)", "Pacific/Pago_Pago (GMT-11)": "Pacific/Pago_Pago (GMT-11)", "Pacific/Honolulu (GMT-10)": "Pacific/Honolulu (GMT-10)", "America/Adak (GMT-10)": "America/Adak (GMT-10; daylight GMT-9)", "America/Anchorage (GMT-9)": "America/Anchorage (GMT-9; daylight GMT-8)", "America/Juneau (GMT-9)": "America/Juneau (GMT-9; daylight GMT-8)", "America/Los_Angeles (GMT-8)": "America/Los_Angeles (GMT-8; daylight GMT-7)", "America/Vancouver (GMT-8)": "America/Vancouver (GMT-8; active GMT-7)", "America/Tijuana (GMT-8)": "America/Tijuana (GMT-8; daylight GMT-7)", "America/Denver (GMT-7)": "America/Denver (GMT-7; daylight GMT-6)", "America/Phoenix (GMT-7)": "America/Phoenix (GMT-7)", "America/Edmonton (GMT-7)": "America/Edmonton (GMT-7; active GMT-6)", "America/Boise (GMT-7)": "America/Boise (GMT-7; daylight GMT-6)", "America/Chicago (GMT-6)": "America/Chicago (GMT-6; daylight GMT-5)", "America/Mexico_City (GMT-6)": "America/Mexico_City (GMT-6)", "America/Winnipeg (GMT-6)": "America/Winnipeg (GMT-6; daylight GMT-5)", "America/Guatemala (GMT-6)": "America/Guatemala (GMT-6)", "America/Costa_Rica (GMT-6)": "America/Costa_Rica (GMT-6)", "America/New_York (GMT-5)": "America/New_York (GMT-5; daylight GMT-4)", "America/Toronto (GMT-5)": "America/Toronto (GMT-5; daylight GMT-4)", "America/Detroit (GMT-5)": "America/Detroit (GMT-5; daylight GMT-4)", "America/Havana (GMT-5)": "America/Havana (GMT-5; daylight GMT-4)", "America/Bogota (GMT-5)": "America/Bogota (GMT-5)", "America/Lima (GMT-5)": "America/Lima (GMT-5)", "America/Jamaica (GMT-5)": "America/Jamaica (GMT-5)", "America/Panama (GMT-5)": "America/Panama (GMT-5)", "America/Halifax (GMT-4)": "America/Halifax (GMT-4; daylight GMT-3)", "America/Caracas (GMT-4)": "America/Caracas (GMT-4)", "America/Santiago (GMT-4)": "America/Santiago (GMT-4; daylight GMT-3)", "America/La_Paz (GMT-4)": "America/La_Paz (GMT-4)", "America/Manaus (GMT-4)": "America/Manaus (GMT-4)", "America/Barbados (GMT-4)": "America/Barbados (GMT-4)", "America/Puerto_Rico (GMT-4)": "America/Puerto_Rico (GMT-4)", "America/Santo_Domingo (GMT-4)": "America/Santo_Domingo (GMT-4)", "America/St_Johns (GMT-3:30)": "America/St_Johns (GMT-3:30; daylight GMT-2:30)", "America/Sao_Paulo (GMT-3)": "America/Sao_Paulo (GMT-3)", "America/Argentina/Buenos_Aires (GMT-3)": "America/Argentina/Buenos_Aires (GMT-3)", "America/Montevideo (GMT-3)": "America/Montevideo (GMT-3)", "America/Paramaribo (GMT-3)": "America/Paramaribo (GMT-3)", "Atlantic/South_Georgia (GMT-2)": "Atlantic/South_Georgia (GMT-2)", "Atlantic/Azores (GMT-1)": "Atlantic/Azores (GMT-1; daylight GMT+0)", "Atlantic/Cape_Verde (GMT-1)": "Atlantic/Cape_Verde (GMT-1)", "UTC (GMT+0)": "UTC (GMT+0)", "Europe/London (GMT+0)": "Europe/London (GMT+0; daylight GMT+1)", "Europe/Dublin (GMT+0)": "Europe/Dublin (GMT+0; daylight GMT+1)", "Europe/Lisbon (GMT+0)": "Europe/Lisbon (GMT+0; daylight GMT+1)", "Africa/Casablanca (GMT+1)": "Africa/Casablanca (GMT+1)", "Africa/Accra (GMT+0)": "Africa/Accra (GMT+0)", "Atlantic/Reykjavik (GMT+0)": "Atlantic/Reykjavik (GMT+0)", "Europe/Paris (GMT+1)": "Europe/Paris (GMT+1; daylight GMT+2)", "Europe/Berlin (GMT+1)": "Europe/Berlin (GMT+1; daylight GMT+2)", "Europe/Rome (GMT+1)": "Europe/Rome (GMT+1; daylight GMT+2)", "Europe/Madrid (GMT+1)": "Europe/Madrid (GMT+1; daylight GMT+2)", "Europe/Amsterdam (GMT+1)": "Europe/Amsterdam (GMT+1; daylight GMT+2)", "Europe/Brussels (GMT+1)": "Europe/Brussels (GMT+1; daylight GMT+2)", "Europe/Vienna (GMT+1)": "Europe/Vienna (GMT+1; daylight GMT+2)", "Europe/Zurich (GMT+1)": "Europe/Zurich (GMT+1; daylight GMT+2)", "Europe/Stockholm (GMT+1)": "Europe/Stockholm (GMT+1; daylight GMT+2)", "Europe/Oslo (GMT+1)": "Europe/Oslo (GMT+1; daylight GMT+2)", "Europe/Copenhagen (GMT+1)": "Europe/Copenhagen (GMT+1; daylight GMT+2)", "Europe/Warsaw (GMT+1)": "Europe/Warsaw (GMT+1; daylight GMT+2)", "Europe/Prague (GMT+1)": "Europe/Prague (GMT+1; daylight GMT+2)", "Europe/Budapest (GMT+1)": "Europe/Budapest (GMT+1; daylight GMT+2)", "Europe/Belgrade (GMT+1)": "Europe/Belgrade (GMT+1; daylight GMT+2)", "Africa/Lagos (GMT+1)": "Africa/Lagos (GMT+1)", "Africa/Tunis (GMT+1)": "Africa/Tunis (GMT+1)", "Africa/Cairo (GMT+2)": "Africa/Cairo (GMT+2; daylight GMT+3)", "Europe/Athens (GMT+2)": "Europe/Athens (GMT+2; daylight GMT+3)", "Europe/Bucharest (GMT+2)": "Europe/Bucharest (GMT+2; daylight GMT+3)", "Europe/Helsinki (GMT+2)": "Europe/Helsinki (GMT+2; daylight GMT+3)", "Europe/Kyiv (GMT+2)": "Europe/Kyiv (GMT+2; daylight GMT+3)", "Europe/Istanbul (GMT+3)": "Europe/Istanbul (GMT+3)", "Africa/Johannesburg (GMT+2)": "Africa/Johannesburg (GMT+2)", "Africa/Nairobi (GMT+3)": "Africa/Nairobi (GMT+3)", "Asia/Jerusalem (GMT+2)": "Asia/Jerusalem (GMT+2; daylight GMT+3)", "Asia/Amman (GMT+3)": "Asia/Amman (GMT+3)", "Asia/Beirut (GMT+2)": "Asia/Beirut (GMT+2; daylight GMT+3)", "Europe/Moscow (GMT+3)": "Europe/Moscow (GMT+3)", "Asia/Baghdad (GMT+3)": "Asia/Baghdad (GMT+3)", "Asia/Riyadh (GMT+3)": "Asia/Riyadh (GMT+3)", "Asia/Kuwait (GMT+3)": "Asia/Kuwait (GMT+3)", "Asia/Qatar (GMT+3)": "Asia/Qatar (GMT+3)", "Africa/Addis_Ababa (GMT+3)": "Africa/Addis_Ababa (GMT+3)", "Asia/Tehran (GMT+3:30)": "Asia/Tehran (GMT+3:30)", "Asia/Dubai (GMT+4)": "Asia/Dubai (GMT+4)", "Asia/Muscat (GMT+4)": "Asia/Muscat (GMT+4)", "Asia/Baku (GMT+4)": "Asia/Baku (GMT+4)", "Asia/Tbilisi (GMT+4)": "Asia/Tbilisi (GMT+4)", "Indian/Mauritius (GMT+4)": "Indian/Mauritius (GMT+4)", "Asia/Kabul (GMT+4:30)": "Asia/Kabul (GMT+4:30)", "Asia/Karachi (GMT+5)": "Asia/Karachi (GMT+5)", "Asia/Tashkent (GMT+5)": "Asia/Tashkent (GMT+5)", "Asia/Yekaterinburg (GMT+5)": "Asia/Yekaterinburg (GMT+5)", "Asia/Kolkata (GMT+5:30)": "Asia/Kolkata (GMT+5:30)", "Asia/Colombo (GMT+5:30)": "Asia/Colombo (GMT+5:30)", "Asia/Kathmandu (GMT+5:45)": "Asia/Kathmandu (GMT+5:45)", "Asia/Dhaka (GMT+6)": "Asia/Dhaka (GMT+6)", "Asia/Almaty (GMT+5)": "Asia/Almaty (GMT+5)", "Asia/Rangoon (GMT+6:30)": "Asia/Rangoon (GMT+6:30)", "Asia/Bangkok (GMT+7)": "Asia/Bangkok (GMT+7)", "Asia/Jakarta (GMT+7)": "Asia/Jakarta (GMT+7)", "Asia/Ho_Chi_Minh (GMT+7)": "Asia/Ho_Chi_Minh (GMT+7)", "Asia/Singapore (GMT+8)": "Asia/Singapore (GMT+8)", "Asia/Kuala_Lumpur (GMT+8)": "Asia/Kuala_Lumpur (GMT+8)", "Asia/Shanghai (GMT+8)": "Asia/Shanghai (GMT+8)", "Asia/Hong_Kong (GMT+8)": "Asia/Hong_Kong (GMT+8)", "Asia/Taipei (GMT+8)": "Asia/Taipei (GMT+8)", "Asia/Manila (GMT+8)": "Asia/Manila (GMT+8)", "Australia/Perth (GMT+8)": "Australia/Perth (GMT+8)", "Asia/Tokyo (GMT+9)": "Asia/Tokyo (GMT+9)", "Asia/Seoul (GMT+9)": "Asia/Seoul (GMT+9)", "Asia/Pyongyang (GMT+9)": "Asia/Pyongyang (GMT+9)", "Australia/Adelaide (GMT+9:30)": "Australia/Adelaide (GMT+9:30; daylight GMT+10:30)", "Australia/Darwin (GMT+9:30)": "Australia/Darwin (GMT+9:30)", "Australia/Sydney (GMT+10)": "Australia/Sydney (GMT+10; daylight GMT+11)", "Australia/Melbourne (GMT+10)": "Australia/Melbourne (GMT+10; daylight GMT+11)", "Australia/Brisbane (GMT+10)": "Australia/Brisbane (GMT+10)", "Australia/Hobart (GMT+10)": "Australia/Hobart (GMT+10; daylight GMT+11)", "Pacific/Guam (GMT+10)": "Pacific/Guam (GMT+10)", "Pacific/Port_Moresby (GMT+10)": "Pacific/Port_Moresby (GMT+10)", "Asia/Vladivostok (GMT+10)": "Asia/Vladivostok (GMT+10)", "Pacific/Noumea (GMT+11)": "Pacific/Noumea (GMT+11)", "Pacific/Norfolk (GMT+11)": "Pacific/Norfolk (GMT+11; daylight GMT+12)", "Asia/Magadan (GMT+11)": "Asia/Magadan (GMT+11)", "Pacific/Auckland (GMT+12)": "Pacific/Auckland (GMT+12; daylight GMT+13)", "Pacific/Fiji (GMT+12)": "Pacific/Fiji (GMT+12)", "Pacific/Chatham (GMT+12:45)": "Pacific/Chatham (GMT+12:45; daylight GMT+13:45)", "Pacific/Tongatapu (GMT+13)": "Pacific/Tongatapu (GMT+13)", "Pacific/Apia (GMT+13)": "Pacific/Apia (GMT+13)", "Pacific/Kiritimati (GMT+14)": "Pacific/Kiritimati (GMT+14)" };
  var frameStatusPath = "/espframe/api/v1/status";
  var NativeSettingsApi = class extends EspframeApiClient {
    async getFrameStatus() {
      const capabilities = await this.negotiate();
      if ((capabilities == null ? void 0 : capabilities.status_path) !== frameStatusPath) return null;
      const payload = await this.requestJson(frameStatusPath, { cache: "no-store" }, "frame_status_failed");
      if (!payload || typeof payload !== "object") throw new Error("invalid_frame_status");
      const status = payload;
      if (status.api_version !== capabilities.api_version || typeof status.restarting !== "boolean" || typeof status.screen !== "string" || typeof status.playback !== "string" || typeof status.source !== "string" || typeof status.immich !== "string" || typeof status.notice !== "string") throw new Error("invalid_frame_status");
      return status;
    }
  };
  var apiClient = new NativeSettingsApi("/espframe/api/v1/capabilities");
  var frameStatusLabels = {
    screen: ["Screen", { starting: "Starting", awake: "On", asleep: "Asleep" }],
    playback: ["Playback", {
      setup: "Waiting for setup",
      loading: "Waiting for the first photo",
      showing: "Showing photos",
      paused: "Paused"
    }],
    source: ["Photo source", {
      none: "None selected",
      all: "All photos",
      favorites: "Favourites",
      album: "Album",
      memories: "On This Day",
      custom: "Custom filters"
    }],
    immich: ["Immich", {
      not_configured: "Setup required",
      waiting_for_network: "Waiting for network or time",
      connecting: "Waiting for a response",
      responding: "Responding",
      rejected: "Access rejected",
      unreachable: "Unreachable",
      request_failed: "Request failed"
    }]
  };
  var frameNotices = {
    waiting_for_time: "Waiting for the frame\u2019s clock to sync.",
    memories_empty: "No On This Day photos in the selected window.",
    memories_unavailable: "On This Day is unavailable. Check whether the Immich key can read memories.",
    incomplete_filters: "Choose values for the enabled photo filters.",
    unsupported_filters: "These filters require a newer Immich server.",
    filters_unavailable: "Check the photo filters and try again.",
    no_photos: "Immich returned no photos for this selection.",
    waiting_for_pair: "Waiting for a matching portrait pair."
  };
  function initNativeFrameStatus() {
    const panel = document.getElementById("native-frame-status");
    if (!panel) return;
    const values = el("dl");
    const fields = /* @__PURE__ */ new Map();
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
    let timer;
    let busy = false;
    let supported = true;
    const setNotice = (message) => {
      if (notice.textContent !== message) notice.textContent = message;
      notice.hidden = !message;
    };
    const render = (status) => {
      for (const [key, value] of Object.entries(status)) {
        const field2 = fields.get(key);
        if (!field2 || typeof value !== "string") continue;
        const label = frameStatusLabels[key][1][value];
        const text = typeof label === "string" ? label : "Unknown";
        if (field2.textContent !== text) field2.textContent = text;
      }
      const detail = frameNotices[status.notice];
      setNotice(status.restarting ? "The frame is restarting." : status.immich === "rejected" ? "Check the Immich API key and its permissions." : typeof detail === "string" ? detail : "");
      values.hidden = status.restarting;
      panel.hidden = false;
    };
    const scheduleRefresh = (delay = 5e3) => {
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
  function delayMs(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }
  var nativeSettingGroups = [
    ["Frame", [["frame_name", "Frame name"]]],
    ["Immich connection", [
      ["immich_url", "Immich server URL"],
      ["api_key", "Replacement Immich API key"],
      ["conn_timeout", "Connection timeout"]
    ]],
    ["Photo preparation service", [["prepared_slide_url", "Service URL"], ["prepared_slide_token", "Service access token"]]],
    ["Playback", [
      ["interval", "Time per photo"],
      ["slideshow_progress_ring_enabled", "Slideshow Progress Ring"],
      ["photo_orientation", "Display photos"],
      ["display_mode", "Photo framing"],
      ["transition", "Slideshow transition"],
      ["transition_speed", "Transition speed"],
      ["portrait_pairing", "Pair portrait photos"],
      ["portrait_pair_framing", "Portrait pair framing"],
      ["portrait_pair_divider", "Portrait pair divider"],
      ["portrait_pairs_only", "Show paired portraits only"],
      ["portrait_pairing_range", "Pairing range"]
    ]],
    ["Capture dates", [
      ["date_filter_enabled", "Limit photos by capture date"],
      ["date_filter_mode", "Date range"],
      ["date_from", "From"],
      ["date_to", "Through"],
      ["relative_amount", "Last"],
      ["relative_unit", "Unit"]
    ]],
    ["On This Day", [["memories_window", "Memories window"], ["memories_fallback", "Fall back to all photos"]]],
    ["Photo captions", [
      ["photo_metadata_date_enabled", "Show date"],
      ["photo_metadata_location_enabled", "Show location"],
      ["photo_metadata_date_format", "Date format"],
      ["photo_metadata_date_taken_format", "Date taken format"]
    ]],
    ["Display", [
      ["device_language", "Display language"],
      ["screen_rotation", "Mounting orientation"],
      ["brightness_day", "Daytime brightness (%)"],
      ["brightness_night", "Nighttime brightness (%)"],
      ["base_tone_enabled", "Adjust display tone"],
      ["base_tone", "Display warmth (%)"],
      ["warm_tones_enabled", "Use warmer tones at night"],
      ["warm_tone_intensity", "Nighttime warmth (%)"],
      ["warm_tone_override", "Warm tone until sunrise"]
    ]],
    ["Firmware", [["auto_update", "Auto Update"], ["update_frequency", "Update Frequency"]]],
    ["Clock", [["show_clock", "Show clock"], ["clock_format", "Time format"], ["timezone", "Timezone"]]],
    ["Sleep schedule", [
      ["schedule_enabled", "Schedule screen off"],
      ["schedule_on_hour", "On hour (0\u201323)"],
      ["schedule_off_hour", "Off hour (0\u201323)"],
      ["schedule_wake_timeout", "Wake duration (seconds)"]
    ]]
  ];
  function nativeSecret(key) {
    return key === "api_key" || key === "prepared_slide_token";
  }
  function nativeSettingControl(key, value, developer) {
    var _a, _b;
    const spec = PRODUCT_SETTINGS[key];
    const options = key === "timezone" ? TIMEZONES.slice() : key === "portrait_pair_framing" ? ["Follow photo framing", "Fill", "Fit"] : key === "portrait_pair_divider" ? ["Black", "White"] : key === "screen_rotation" && !developer ? ["0", "180"] : (_b = (_a = spec == null ? void 0 : spec.options) == null ? void 0 : _a.slice()) != null ? _b : [];
    if (options.length) {
      const current = String(value);
      if (!options.includes(current)) options.push(current);
      return selectFromOptions(options, current, function() {
      }, function(option) {
        if (key === "timezone") return (TIMEZONE_LABELS[option] || option).replace(/_/g, " ");
        if (key === "device_language") return new Intl.DisplayNames([option], { type: "language", languageDisplay: "standard" }).of(option) || option;
        if (key === "display_mode") return option === "Fit" ? "Show full image" : "Crop to fit";
        if (key === "portrait_pair_framing" && option !== "Follow photo framing") {
          return option === "Fill" ? "Fill each half" : "Fit both full photos";
        }
        if (key === "screen_rotation") return option === "0" ? "Normal" : option === "180" ? "Upside down (180\xB0)" : "Portrait (" + option + "\xB0)";
        return option;
      });
    }
    const control = el("input");
    if (typeof value === "boolean") {
      control.type = "checkbox";
      control.checked = value;
    } else if (typeof value === "number") {
      control.type = "number";
      if ((spec == null ? void 0 : spec.min) !== void 0) control.min = String(spec.min);
      if ((spec == null ? void 0 : spec.max) !== void 0) control.max = String(spec.max);
      control.step = "1";
      control.required = true;
      control.value = String(value);
    } else {
      const url = key === "immich_url" || key === "prepared_slide_url";
      control.type = url ? "url" : nativeSecret(key) ? "password" : key === "date_from" || key === "date_to" ? "date" : "text";
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
  function nativeSettingsForm(snapshot, savedNotice = "") {
    var _a, _b, _c, _d, _e, _f, _g;
    const frameName = typeof snapshot.values.frame_name === "string" ? snapshot.values.frame_name : "";
    document.title = frameName ? `${frameName} \xB7 Espframe` : "Espframe settings";
    const form = el("form");
    form.id = "native-settings";
    const fields = el("div");
    const controls = /* @__PURE__ */ new Map();
    let saving = false;
    const unavailable = new Set(snapshot.unavailable);
    for (const [title, definitions] of nativeSettingGroups) {
      const body = el("div");
      for (const [key, label] of definitions) {
        const snapshotKey = key === "prepared_slide_token" ? "prepared_slide_url" : key;
        if (snapshotKey === "prepared_slide_url" && !snapshot.values.prepared_slide_url) continue;
        if (unavailable.has(key) || key !== "api_key" && !Object.prototype.hasOwnProperty.call(snapshot.values, snapshotKey)) continue;
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
          if (key === "api_key") hint.textContent = snapshot.api_key_configured ? "Leave blank to keep the current key." : "Enter your API key to connect Immich.";
          if (key === "frame_name") hint.textContent = "Leave blank to use the device default.";
          if (key === "prepared_slide_url") hint.textContent = "Clear the URL to return to direct Immich previews.";
          if (key === "prepared_slide_token") hint.textContent = snapshot.values.prepared_slide_token_configured ? "Leave blank to keep the current token. Enter a new token if you change the service URL." : "Enter the token from your preparation service.";
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
      const changed = {};
      controls.forEach(({ element, value }, key) => {
        const current = element instanceof HTMLInputElement && element.type === "checkbox" ? element.checked : element instanceof HTMLInputElement && element.type === "number" ? element.valueAsNumber : key === "frame_name" ? element.value.replace(/^[ \t\r\n\f\v]+|[ \t\r\n\f\v]+$/g, "") : element.value.trim();
        if (current !== value) changed[key] = current;
      });
      return changed;
    }
    const pairing = (_a = controls.get("portrait_pairing")) == null ? void 0 : _a.element;
    const pairingOptions = [
      (_b = controls.get("portrait_pairs_only")) == null ? void 0 : _b.element,
      (_c = controls.get("portrait_pairing_range")) == null ? void 0 : _c.element,
      (_d = controls.get("portrait_pair_framing")) == null ? void 0 : _d.element,
      (_e = controls.get("portrait_pair_divider")) == null ? void 0 : _e.element
    ];
    const preparedUrl = controls.get("prepared_slide_url");
    const preparedToken = (_f = controls.get("prepared_slide_token")) == null ? void 0 : _f.element;
    const nameInput = (_g = controls.get("frame_name")) == null ? void 0 : _g.element;
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
        nameInput.setCustomValidity(new TextEncoder().encode(name).length > 120 || /[\u0000-\u001f\u007f-\u009f]/.test(name) ? "Use up to 120 UTF-8 bytes without control characters." : "");
      }
      save.disabled = saving || Object.keys(changes()).length === 0;
    };
    const setSaving = (value) => {
      saving = value;
      controls.forEach(({ element }) => {
        element.disabled = value;
      });
      noteChange();
    };
    form.addEventListener("input", noteChange);
    form.addEventListener("change", noteChange);
    noteChange();
    form.onsubmit = async function(event) {
      var _a2, _b2;
      event.preventDefault();
      if (saving || !form.reportValidity()) return;
      const values = changes();
      if (!Object.keys(values).length) return;
      setSaving(true);
      setStatus(status, "Saving settings\u2026");
      let hostname = "";
      let restartRequired = true;
      try {
        const response = await apiClient.updateSettings(values, []);
        if (!response || response.status !== "accepted") throw new Error("save_failed");
        restartRequired = response.restart_required !== false;
        if (typeof response.frame_hostname === "string" && /^[a-z0-9-]{1,63}$/.test(response.frame_hostname))
          hostname = response.frame_hostname + ".local";
      } catch (error) {
        const label = error instanceof EspframeApiError && error.field ? (_b2 = form.querySelector(`label[for="${((_a2 = controls.get(error.field)) == null ? void 0 : _a2.element.id) || ""}"]`)) == null ? void 0 : _b2.textContent : "";
        const message = label ? `Check the value for ${label}.` : error instanceof EspframeApiError && error.kind === "validation" ? "Check your settings and try again." : "Settings could not be confirmed. Check the frame connection and try again.";
        setStatus(status, message, "red");
        setSaving(false);
        return;
      }
      controls.forEach(({ element }, key) => {
        if (nativeSecret(key)) element.value = "";
      });
      if (!restartRequired) {
        try {
          form.replaceWith(nativeSettingsForm(await apiClient.getConfigurationSnapshot(), "Settings saved."));
        } catch (_) {
          setStatus(status, "Settings saved. Reload this page to see the current values.");
          setSaving(false);
        }
        return;
      }
      setStatus(status, "Settings saved. The frame is restarting\u2026");
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
    status.textContent = "Loading settings\u2026";
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
})();
