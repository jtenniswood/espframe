// ESPFRAME: generated from typed docs/webserver/src and product/contract; run `npm run generate` to update.
(() => {
  var __defProp = Object.defineProperty;
  var __defNormalProp = (obj, key, value) => key in obj ? __defProp(obj, key, { enumerable: true, configurable: true, writable: true, value }) : obj[key] = value;
  var __publicField = (obj, key, value) => __defNormalProp(obj, typeof key !== "symbol" ? key + "" : key, value);

  // docs/webserver/src/api_client.ts
  var EspframeApiError = class extends Error {
    constructor(kind, message, status, code, field2) {
      super(message);
      __publicField(this, "kind", kind);
      __publicField(this, "status", status);
      __publicField(this, "code", code);
      __publicField(this, "field", field2);
      this.name = "EspframeApiError";
    }
  };
  function object(value) {
    return typeof value === "object" && value !== null && !Array.isArray(value);
  }
  var EspframeApiClient = class {
    constructor(capabilitiesPath, timeoutMs = 5e3) {
      __publicField(this, "capabilitiesPath", capabilitiesPath);
      __publicField(this, "timeoutMs", timeoutMs);
      __publicField(this, "queue", Promise.resolve());
      __publicField(this, "capabilities", null);
      __publicField(this, "negotiated");
      __publicField(this, "resetEpoch");
      __publicField(this, "resetAccepted", false);
    }
    waitForWrites() {
      return this.queue;
    }
    enqueue(command) {
      const request = this.queue.then(command);
      this.queue = request.catch(() => void 0);
      return request;
    }
    error(status, message, payload) {
      const body = object(payload) ? payload : {};
      const kind = status === 404 || status === 405 ? "unavailable" : status === 409 ? "conflict" : status === 400 || status === 422 ? "validation" : "server";
      return new EspframeApiError(
        kind,
        typeof body.error === "string" ? body.error : message,
        status,
        typeof body.error === "string" ? body.error : void 0,
        typeof body.field === "string" ? body.field : void 0
      );
    }
    async requestWith(url, init, consume) {
      const controller = new AbortController();
      const timer = setTimeout(() => controller.abort(), this.timeoutMs);
      try {
        const response = await fetch(url, { ...init, signal: controller.signal });
        return await consume(response);
      } catch (error) {
        if (error instanceof EspframeApiError) throw error;
        if (error && typeof error === "object" && error.name === "AbortError") {
          throw new EspframeApiError("timeout", "request_timeout");
        }
        throw new EspframeApiError("offline", "device_offline");
      } finally {
        clearTimeout(timer);
      }
    }
    request(url, init = {}) {
      return this.requestWith(url, init, async (response) => response);
    }
    requestJson(url, init, message) {
      return this.requestWith(url, init, (response) => this.json(response, message));
    }
    async json(response, message) {
      let payload = null;
      try {
        payload = await response.json();
      } catch (error) {
        if (error && typeof error === "object" && error.name === "AbortError") {
          throw new EspframeApiError("timeout", "request_timeout");
        }
        if (!response.ok) throw this.error(response.status, message);
        throw new EspframeApiError("server", "invalid_server_response", response.status);
      }
      if (!response.ok) throw this.error(response.status, message, payload);
      return payload;
    }
    parseCapabilities(value) {
      if (!object(value) || typeof value.contract_version !== "number" || typeof value.api_version !== "number" || typeof value.configuration_path !== "string" || value.update_mode !== "atomic" || typeof value.configuration_available !== "boolean" || typeof value.configuration_read !== "boolean" || typeof value.configuration_write !== "boolean" || typeof value.legacy_entity_api !== "boolean") return null;
      return value;
    }
    negotiate() {
      if (this.negotiated !== void 0) return Promise.resolve(this.negotiated);
      if (!this.capabilities) {
        this.capabilities = this.requestJson(this.capabilitiesPath, { cache: "no-store" }, "capabilities_unavailable").then((payload) => {
          const capabilities = this.parseCapabilities(payload);
          if (!capabilities) {
            this.negotiated = null;
            return null;
          }
          this.negotiated = capabilities.configuration_available && capabilities.configuration_read && capabilities.configuration_write ? capabilities : null;
          return this.negotiated;
        }).catch((error) => {
          if (error instanceof EspframeApiError && error.kind === "unavailable") {
            this.negotiated = null;
            return null;
          }
          this.capabilities = null;
          throw error;
        });
      }
      return this.capabilities;
    }
    async getConfigurationSnapshot() {
      const capabilities = await this.negotiate();
      if (!capabilities) throw new EspframeApiError("unavailable", "configuration_api_unavailable");
      const payload = await this.requestJson(capabilities.configuration_path, { cache: "no-store" }, "configuration_api_failed");
      if (!object(payload) || payload.api_version !== capabilities.api_version || capabilities.reset_path && !Number.isInteger(payload.reset_epoch) || !object(payload.values) || !Array.isArray(payload.unavailable) || !payload.unavailable.every((value) => typeof value === "string")) {
        throw new EspframeApiError("server", "invalid_configuration_snapshot");
      }
      let apiKeyConfigured = typeof payload.api_key_configured === "boolean" ? payload.api_key_configured : void 0;
      const values = {};
      for (const [key, value] of Object.entries(payload.values)) {
        if (key === "api_key") {
          if (apiKeyConfigured !== void 0) throw new EspframeApiError("server", "invalid_configuration_snapshot");
          apiKeyConfigured = !!value;
          continue;
        }
        if (typeof value !== "string" && typeof value !== "number" && typeof value !== "boolean") {
          throw new EspframeApiError("server", "invalid_configuration_snapshot");
        }
        values[key] = value;
      }
      if (apiKeyConfigured === void 0) apiKeyConfigured = false;
      this.resetEpoch = Number.isInteger(payload.reset_epoch) ? payload.reset_epoch : 0;
      return {
        api_version: capabilities.api_version,
        reset_epoch: this.resetEpoch,
        api_key_configured: apiKeyConfigured,
        values,
        unavailable: payload.unavailable
      };
    }
    async legacyPost(url, body) {
      return this.requestWith(url, body === void 0 ? { method: "POST" } : {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body
      }, async (response) => {
        if (!response.ok) {
          let payload = null;
          try {
            payload = await response.json();
          } catch (error) {
            if (error && typeof error === "object" && error.name === "AbortError") {
              throw new EspframeApiError("timeout", "request_timeout");
            }
          }
          throw this.error(response.status, "legacy_write_failed", payload);
        }
        return response;
      });
    }
    async legacyWrite(setting) {
      if (setting.domain === "switch") {
        await this.legacyPost(setting.url + (setting.value ? "/turn_on" : "/turn_off"));
        return;
      }
      const body = new URLSearchParams({ [setting.domain === "select" ? "option" : "value"]: String(setting.value) }).toString();
      await this.legacyPost(setting.url + "/set", body);
    }
    updateSettings(values, legacy, compatibilityValues = values) {
      return this.enqueue(async () => {
        if (this.resetAccepted) throw new EspframeApiError("conflict", "reset_pending", 409, "reset_pending");
        const capabilities = await this.negotiate();
        if (!capabilities) {
          for (const setting of legacy) await this.legacyWrite(setting);
          return null;
        }
        if (this.resetEpoch === void 0) await this.getConfigurationSnapshot();
        const supportedValues = capabilities.firmware_rotation_policy === true ? values : compatibilityValues;
        const body = new URLSearchParams({ [capabilities.configuration_parameter || "configuration"]: JSON.stringify({ api_version: capabilities.api_version, reset_epoch: this.resetEpoch, values: supportedValues }) }).toString();
        try {
          const payload = await this.requestJson(capabilities.configuration_path, {
            method: "POST",
            headers: { "Content-Type": capabilities.configuration_encoding || "application/x-www-form-urlencoded" },
            body
          }, "configuration_update_failed");
          if (!object(payload) || payload.status !== "accepted") throw new EspframeApiError("server", "configuration_update_failed");
          await new Promise((resolve) => setTimeout(resolve, 100));
          return payload;
        } catch (error) {
          if (!(error instanceof EspframeApiError) || error.kind !== "unavailable") throw error;
          this.negotiated = null;
          for (const setting of legacy) await this.legacyWrite(setting);
          return null;
        }
      });
    }
    reset(mode) {
      return this.enqueue(async () => {
        if (this.resetAccepted) return;
        const capabilities = await this.negotiate();
        if (!capabilities || !capabilities.reset_path) {
          throw new EspframeApiError("unavailable", "reset_unavailable");
        }
        await this.legacyPost(capabilities.reset_path, new URLSearchParams({ mode }).toString());
        this.resetAccepted = true;
      });
    }
    post(url, params) {
      if (this.resetAccepted) return Promise.reject(new EspframeApiError("conflict", "reset_pending", 409, "reset_pending"));
      const query = params ? "?" + new URLSearchParams(Object.entries(params).map(([key, value]) => [key, String(value)])).toString() : "";
      return this.enqueue(() => this.legacyPost(url + query));
    }
    postText(url, value, useQueryFallback = false) {
      if (this.resetAccepted) return Promise.reject(new EspframeApiError("conflict", "reset_pending", 409, "reset_pending"));
      return this.enqueue(() => {
        const body = new URLSearchParams({ value }).toString();
        const query = useQueryFallback && (url + "?" + body).length <= 120 ? "?" + body : "";
        return this.legacyPost(url + query, body);
      });
    }
    getJson(url) {
      return this.requestJson(url, { cache: "no-store" }, "legacy_read_failed");
    }
  };

  // docs/webserver/src/shared_controls.ts
  function selectFromOptions(options, current, onChange, optionDisplayFn) {
    var display = optionDisplayFn || function(o) {
      return o;
    };
    var sel = document.createElement("select");
    sel.className = "select";
    options.forEach(function(o) {
      var opt = document.createElement("option");
      opt.value = o;
      opt.textContent = display(o);
      if (o === current) opt.selected = true;
      sel.appendChild(opt);
    });
    sel.onchange = function() {
      onChange(sel.value);
    };
    return sel;
  }
  function el(tag, cls) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    return e;
  }
  function setStatus(target, msg, type, clearAfterMs) {
    if (!target) return;
    target.replaceChildren();
    if (!msg) {
      target.textContent = "";
      return;
    }
    var dot = el("span", "dot " + (type || "green"));
    target.appendChild(dot);
    target.appendChild(document.createTextNode(" " + msg));
    clearTimeout(target._t);
    if (clearAfterMs) {
      target._t = setTimeout(function() {
        target.textContent = "";
      }, clearAfterMs);
    }
  }
  function button(text, cls, onClick) {
    var btn = el("button", cls || "btn btn-secondary");
    btn.type = "button";
    btn.textContent = text;
    if (onClick) btn.onclick = onClick;
    return btn;
  }
  var controlId = 0;
  function bindDisclosure(toggle, panel, body, className, expandedWhenPresent) {
    if (!body.id) body.id = "disclosure-" + ++controlId;
    toggle.setAttribute("aria-controls", body.id);
    function syncExpanded() {
      toggle.setAttribute("aria-expanded", String(panel.classList.contains(className) === expandedWhenPresent));
    }
    syncExpanded();
    toggle.onclick = function(event) {
      event.stopPropagation();
      panel.classList.toggle(className);
      syncExpanded();
    };
  }
  function connectFieldLabels(root) {
    root.querySelectorAll(".field > label").forEach(function(label) {
      var control = label.parentElement.querySelector("input,select,textarea");
      if (!control) return;
      if (!control.id) control.id = "setting-" + ++controlId;
      label.htmlFor = control.id;
    });
  }
  function makeCollapsibleCard(title, bodyElement, defaultCollapsed, badgeEl) {
    var card = el("div", "card");
    var header = el("div", "card-header");
    var h3 = document.createElement("h3");
    var toggle = el("button", "card-toggle");
    toggle.type = "button";
    toggle.textContent = title;
    h3.appendChild(toggle);
    var rightWrap = el("div", "card-header-right");
    if (badgeEl) rightWrap.appendChild(badgeEl);
    var chevron = el("span", "card-chevron");
    chevron.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M6 9l6 6 6-6"/></svg>';
    rightWrap.appendChild(chevron);
    header.appendChild(h3);
    header.appendChild(rightWrap);
    var body = el("div", "card-body");
    body.appendChild(bodyElement);
    card.appendChild(header);
    card.appendChild(body);
    if (defaultCollapsed) card.classList.add("collapsed");
    bindDisclosure(toggle, card, body, "collapsed", false);
    header.onclick = function() {
      toggle.click();
    };
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

  // docs/webserver/src/app.ts
  var PRODUCT_SETTINGS = { "photo_source": { "entity": "select/Photos: Source", "domain": "select", "default": "All Photos", "options": ["All Photos", "Favorites", "Album", "Person", "Tag", "Memories", "Custom"] }, "memories_window": { "entity": "select/Photos: Memories Window", "domain": "select", "default": "Within 2 Days", "options": ["Same Day", "Within 1 Day", "Within 2 Days", "Within 3 Days", "Within 7 Days"] }, "memories_fallback": { "entity": "switch/Photos: Memories Fallback", "domain": "switch", "default": true, "options": [] }, "album_order": { "entity": "select/Photos: Album Order", "domain": "select", "default": "Random albums", "options": ["Random albums", "Album list order"] }, "tag_matching": { "entity": "select/Photos: Tag Matching", "domain": "select", "default": "Any selected tag", "options": ["Any selected tag", "All selected tags"] }, "date_filter_mode": { "entity": "select/Photos: Date Filter Mode", "domain": "select", "default": "Fixed Range", "options": ["Fixed Range", "Relative Range"] }, "relative_unit": { "entity": "select/Photos: Relative Unit", "domain": "select", "default": "Years", "options": ["Months", "Years"] }, "photo_orientation": { "entity": "select/Photos: Orientation", "domain": "select", "default": "Any", "options": ["Any", "Portrait Only", "Landscape Only"] }, "display_mode": { "entity": "select/Photos: Display Mode", "domain": "select", "default": "Fill", "options": ["Fill", "Fit"] }, "transition": { "entity": "select/Photos: Transition", "domain": "select", "default": "Cross Fade", "options": ["None", "Cross Fade", "Fade to Black", "Fade to White", "Slide Left", "Slide Right", "Slide Up", "Slide Down", "Random"] }, "transition_speed": { "entity": "select/Photos: Transition Speed", "domain": "select", "default": "Fast", "options": ["Fast", "Normal", "Slow"] }, "interval": { "entity": "select/Photos: Slideshow Interval", "domain": "select", "default": "15 seconds", "options": ["10 seconds", "15 seconds", "20 seconds", "30 seconds", "45 seconds", "1 minute", "2 minutes", "3 minutes", "5 minutes", "10 minutes", "15 minutes", "30 minutes", "1 hour", "2 hours", "4 hours", "8 hours", "16 hours", "24 hours"] }, "conn_timeout": { "entity": "select/Screen: Connection Timeout", "domain": "select", "default": "10 minutes", "options": ["30 seconds", "45 seconds", "1 minute", "2 minutes", "3 minutes", "5 minutes", "10 minutes", "15 minutes", "20 minutes", "30 minutes"] }, "screen_rotation": { "entity": "select/Screen: Rotation", "domain": "select", "default": "0", "options": ["0", "180"], "developerOptions": ["90", "270"] }, "photo_metadata_date_format": { "entity": "select/Device: Metadata Date Format", "domain": "select", "default": "Date Taken", "options": ["Relative Date", "Date Taken"] }, "photo_metadata_date_taken_format": { "entity": "select/Device: Metadata Date Taken Format", "domain": "select", "default": "1 January, 2026", "options": ["1 January, 2026", "January 1, 2026"] }, "clock_format": { "entity": "select/Clock: Format", "domain": "select", "default": "24 Hour", "options": ["24 Hour", "12 Hour"] }, "update_frequency": { "entity": "select/Firmware: Update Frequency", "domain": "select", "default": "Daily", "options": ["Hourly", "Daily", "Weekly", "Monthly"] }, "auto_update": { "entity": "switch/Firmware: Auto Update", "domain": "switch", "default": true, "options": [] }, "c6_auto_update": { "entity": "switch/WiFi Firmware: Auto Update", "domain": "switch", "default": true, "options": [] }, "date_filter_enabled": { "entity": "switch/Photos: Date Filter", "domain": "switch", "default": false, "options": [] }, "date_from": { "entity": "text/Photos: Date From", "domain": "text", "default": "", "options": [], "maxLength": 10 }, "date_to": { "entity": "text/Photos: Date To", "domain": "text", "default": "", "options": [], "maxLength": 10 }, "relative_amount": { "entity": "number/Photos: Relative Amount", "domain": "number", "default": 1, "options": [], "min": 1, "max": 120, "step": 1 }, "schedule_enabled": { "entity": "switch/Screen: Schedule Enabled", "domain": "switch", "default": false, "options": [] }, "schedule_on_hour": { "entity": "number/Screen: Schedule On Hour", "domain": "number", "default": 6, "options": [], "min": 0, "max": 23, "step": 1 }, "schedule_off_hour": { "entity": "number/Screen: Schedule Off Hour", "domain": "number", "default": 23, "options": [], "min": 0, "max": 23, "step": 1 }, "schedule_wake_timeout": { "entity": "number/Screen: Schedule Wake Timeout", "domain": "number", "default": 60, "options": [], "min": 10, "max": 3600, "step": 10 }, "brightness_day": { "entity": "number/Screen: Daytime Brightness", "domain": "number", "default": 100, "options": [], "min": 10, "max": 100, "step": 5 }, "brightness_night": { "entity": "number/Screen: Nighttime Brightness", "domain": "number", "default": 75, "options": [], "min": 10, "max": 100, "step": 5 }, "base_tone_enabled": { "entity": "switch/Screen: Tone Adjustment", "domain": "switch", "default": false, "options": [] }, "base_tone": { "entity": "number/Screen: Display Tone", "domain": "number", "default": 0, "options": [], "min": 0, "max": 100, "step": 5 }, "warm_tones_enabled": { "entity": "switch/Screen: Night Tone Adjustment", "domain": "switch", "default": false, "options": [] }, "warm_tone_intensity": { "entity": "number/Screen: Warm Tone Intensity", "domain": "number", "default": 50, "options": [], "min": 10, "max": 100, "step": 5 }, "warm_tone_override": { "entity": "switch/Screen: Warm Tone Override", "domain": "switch", "default": false, "options": [] }, "portrait_pairing": { "entity": "switch/Photos: Portrait Pairing", "domain": "switch", "default": true, "options": [] }, "portrait_pairing_range": { "entity": "select/Photos: Portrait Pairing Range", "domain": "select", "default": "Same Day", "options": ["Same Day", "Within 1 Day", "Within 2 Days"] }, "portrait_pairs_only": { "entity": "switch/Photos: Paired Portraits Only", "domain": "switch", "default": false, "options": [] }, "photo_metadata_date_enabled": { "entity": "switch/Device: Metadata Date", "domain": "switch", "default": true, "options": [] }, "photo_metadata_location_enabled": { "entity": "switch/Device: Metadata Location", "domain": "switch", "default": true, "options": [] }, "albums_enabled": { "entity": "switch/Photos: Albums Enabled", "domain": "switch", "default": false, "options": [] }, "people_enabled": { "entity": "switch/Photos: People Enabled", "domain": "switch", "default": false, "options": [] }, "tags_enabled": { "entity": "switch/Photos: Tags Enabled", "domain": "switch", "default": false, "options": [] }, "favorites_enabled": { "entity": "switch/Photos: Favorites Enabled", "domain": "switch", "default": false, "options": [] }, "rating_enabled": { "entity": "switch/Photos: Rating Enabled", "domain": "switch", "default": false, "options": [] }, "location_enabled": { "entity": "switch/Photos: Location Enabled", "domain": "switch", "default": false, "options": [] }, "inclusion_matching": { "entity": "select/Photos: Inclusion Groups", "domain": "select", "default": "Match all enabled groups", "options": ["Match all enabled groups", "Match any enabled group"] }, "album_matching": { "entity": "select/Photos: Album Matching", "domain": "select", "default": "Any selected album", "options": ["Any selected album", "All selected albums"] }, "person_matching": { "entity": "select/Photos: Person Matching", "domain": "select", "default": "Any selected person", "options": ["Any selected person", "All selected people"] }, "favorite_mode": { "entity": "select/Photos: Favorites", "domain": "select", "default": "Any", "options": ["Any", "Favorites only", "Exclude favorites"] }, "minimum_rating": { "entity": "select/Photos: Minimum Rating", "domain": "select", "default": "Any", "options": ["Any", "1+", "2+", "3+", "4+", "5+"] }, "filter_country": { "entity": "text/Photos: Country", "domain": "text", "default": "", "options": [], "maxLength": 96 }, "filter_state": { "entity": "text/Photos: State or Province", "domain": "text", "default": "", "options": [], "maxLength": 96 }, "filter_city": { "entity": "text/Photos: City", "domain": "text", "default": "", "options": [], "maxLength": 96 }, "device_language": { "entity": "select/Device: Language", "domain": "select", "default": "en", "options": ["en", "cs", "da", "de", "es", "fi", "fr", "he", "hu", "it", "nb", "nl", "pl", "pt", "pt-br", "ro", "ru", "sk", "sl", "sv", "tr", "uk"] } };
  var TIMEZONES = ["Pacific/Midway (GMT-11)", "Pacific/Pago_Pago (GMT-11)", "Pacific/Honolulu (GMT-10)", "America/Adak (GMT-10)", "America/Anchorage (GMT-9)", "America/Juneau (GMT-9)", "America/Los_Angeles (GMT-8)", "America/Vancouver (GMT-8)", "America/Tijuana (GMT-8)", "America/Denver (GMT-7)", "America/Phoenix (GMT-7)", "America/Edmonton (GMT-7)", "America/Boise (GMT-7)", "America/Chicago (GMT-6)", "America/Mexico_City (GMT-6)", "America/Winnipeg (GMT-6)", "America/Guatemala (GMT-6)", "America/Costa_Rica (GMT-6)", "America/New_York (GMT-5)", "America/Toronto (GMT-5)", "America/Detroit (GMT-5)", "America/Havana (GMT-5)", "America/Bogota (GMT-5)", "America/Lima (GMT-5)", "America/Jamaica (GMT-5)", "America/Panama (GMT-5)", "America/Halifax (GMT-4)", "America/Caracas (GMT-4)", "America/Santiago (GMT-4)", "America/La_Paz (GMT-4)", "America/Manaus (GMT-4)", "America/Barbados (GMT-4)", "America/Puerto_Rico (GMT-4)", "America/Santo_Domingo (GMT-4)", "America/St_Johns (GMT-3:30)", "America/Sao_Paulo (GMT-3)", "America/Argentina/Buenos_Aires (GMT-3)", "America/Montevideo (GMT-3)", "America/Paramaribo (GMT-3)", "Atlantic/South_Georgia (GMT-2)", "Atlantic/Azores (GMT-1)", "Atlantic/Cape_Verde (GMT-1)", "UTC (GMT+0)", "Europe/London (GMT+0)", "Europe/Dublin (GMT+0)", "Europe/Lisbon (GMT+0)", "Africa/Casablanca (GMT+1)", "Africa/Accra (GMT+0)", "Atlantic/Reykjavik (GMT+0)", "Europe/Paris (GMT+1)", "Europe/Berlin (GMT+1)", "Europe/Rome (GMT+1)", "Europe/Madrid (GMT+1)", "Europe/Amsterdam (GMT+1)", "Europe/Brussels (GMT+1)", "Europe/Vienna (GMT+1)", "Europe/Zurich (GMT+1)", "Europe/Stockholm (GMT+1)", "Europe/Oslo (GMT+1)", "Europe/Copenhagen (GMT+1)", "Europe/Warsaw (GMT+1)", "Europe/Prague (GMT+1)", "Europe/Budapest (GMT+1)", "Europe/Belgrade (GMT+1)", "Africa/Lagos (GMT+1)", "Africa/Tunis (GMT+1)", "Africa/Cairo (GMT+2)", "Europe/Athens (GMT+2)", "Europe/Bucharest (GMT+2)", "Europe/Helsinki (GMT+2)", "Europe/Kyiv (GMT+2)", "Europe/Istanbul (GMT+3)", "Africa/Johannesburg (GMT+2)", "Africa/Nairobi (GMT+3)", "Asia/Jerusalem (GMT+2)", "Asia/Amman (GMT+3)", "Asia/Beirut (GMT+2)", "Europe/Moscow (GMT+3)", "Asia/Baghdad (GMT+3)", "Asia/Riyadh (GMT+3)", "Asia/Kuwait (GMT+3)", "Asia/Qatar (GMT+3)", "Africa/Addis_Ababa (GMT+3)", "Asia/Tehran (GMT+3:30)", "Asia/Dubai (GMT+4)", "Asia/Muscat (GMT+4)", "Asia/Baku (GMT+4)", "Asia/Tbilisi (GMT+4)", "Indian/Mauritius (GMT+4)", "Asia/Kabul (GMT+4:30)", "Asia/Karachi (GMT+5)", "Asia/Tashkent (GMT+5)", "Asia/Yekaterinburg (GMT+5)", "Asia/Kolkata (GMT+5:30)", "Asia/Colombo (GMT+5:30)", "Asia/Kathmandu (GMT+5:45)", "Asia/Dhaka (GMT+6)", "Asia/Almaty (GMT+5)", "Asia/Rangoon (GMT+6:30)", "Asia/Bangkok (GMT+7)", "Asia/Jakarta (GMT+7)", "Asia/Ho_Chi_Minh (GMT+7)", "Asia/Singapore (GMT+8)", "Asia/Kuala_Lumpur (GMT+8)", "Asia/Shanghai (GMT+8)", "Asia/Hong_Kong (GMT+8)", "Asia/Taipei (GMT+8)", "Asia/Manila (GMT+8)", "Australia/Perth (GMT+8)", "Asia/Tokyo (GMT+9)", "Asia/Seoul (GMT+9)", "Asia/Pyongyang (GMT+9)", "Australia/Adelaide (GMT+9:30)", "Australia/Darwin (GMT+9:30)", "Australia/Sydney (GMT+10)", "Australia/Melbourne (GMT+10)", "Australia/Brisbane (GMT+10)", "Australia/Hobart (GMT+10)", "Pacific/Guam (GMT+10)", "Pacific/Port_Moresby (GMT+10)", "Asia/Vladivostok (GMT+10)", "Pacific/Noumea (GMT+11)", "Pacific/Norfolk (GMT+11)", "Asia/Magadan (GMT+11)", "Pacific/Auckland (GMT+12)", "Pacific/Fiji (GMT+12)", "Pacific/Chatham (GMT+12:45)", "Pacific/Tongatapu (GMT+13)", "Pacific/Apia (GMT+13)", "Pacific/Kiritimati (GMT+14)"];
  var TIMEZONE_LABELS = { "Pacific/Midway (GMT-11)": "Pacific/Midway (GMT-11)", "Pacific/Pago_Pago (GMT-11)": "Pacific/Pago_Pago (GMT-11)", "Pacific/Honolulu (GMT-10)": "Pacific/Honolulu (GMT-10)", "America/Adak (GMT-10)": "America/Adak (GMT-10; daylight GMT-9)", "America/Anchorage (GMT-9)": "America/Anchorage (GMT-9; daylight GMT-8)", "America/Juneau (GMT-9)": "America/Juneau (GMT-9; daylight GMT-8)", "America/Los_Angeles (GMT-8)": "America/Los_Angeles (GMT-8; daylight GMT-7)", "America/Vancouver (GMT-8)": "America/Vancouver (GMT-8; active GMT-7)", "America/Tijuana (GMT-8)": "America/Tijuana (GMT-8; daylight GMT-7)", "America/Denver (GMT-7)": "America/Denver (GMT-7; daylight GMT-6)", "America/Phoenix (GMT-7)": "America/Phoenix (GMT-7)", "America/Edmonton (GMT-7)": "America/Edmonton (GMT-7; active GMT-6)", "America/Boise (GMT-7)": "America/Boise (GMT-7; daylight GMT-6)", "America/Chicago (GMT-6)": "America/Chicago (GMT-6; daylight GMT-5)", "America/Mexico_City (GMT-6)": "America/Mexico_City (GMT-6)", "America/Winnipeg (GMT-6)": "America/Winnipeg (GMT-6; daylight GMT-5)", "America/Guatemala (GMT-6)": "America/Guatemala (GMT-6)", "America/Costa_Rica (GMT-6)": "America/Costa_Rica (GMT-6)", "America/New_York (GMT-5)": "America/New_York (GMT-5; daylight GMT-4)", "America/Toronto (GMT-5)": "America/Toronto (GMT-5; daylight GMT-4)", "America/Detroit (GMT-5)": "America/Detroit (GMT-5; daylight GMT-4)", "America/Havana (GMT-5)": "America/Havana (GMT-5; daylight GMT-4)", "America/Bogota (GMT-5)": "America/Bogota (GMT-5)", "America/Lima (GMT-5)": "America/Lima (GMT-5)", "America/Jamaica (GMT-5)": "America/Jamaica (GMT-5)", "America/Panama (GMT-5)": "America/Panama (GMT-5)", "America/Halifax (GMT-4)": "America/Halifax (GMT-4; daylight GMT-3)", "America/Caracas (GMT-4)": "America/Caracas (GMT-4)", "America/Santiago (GMT-4)": "America/Santiago (GMT-4; daylight GMT-3)", "America/La_Paz (GMT-4)": "America/La_Paz (GMT-4)", "America/Manaus (GMT-4)": "America/Manaus (GMT-4)", "America/Barbados (GMT-4)": "America/Barbados (GMT-4)", "America/Puerto_Rico (GMT-4)": "America/Puerto_Rico (GMT-4)", "America/Santo_Domingo (GMT-4)": "America/Santo_Domingo (GMT-4)", "America/St_Johns (GMT-3:30)": "America/St_Johns (GMT-3:30; daylight GMT-2:30)", "America/Sao_Paulo (GMT-3)": "America/Sao_Paulo (GMT-3)", "America/Argentina/Buenos_Aires (GMT-3)": "America/Argentina/Buenos_Aires (GMT-3)", "America/Montevideo (GMT-3)": "America/Montevideo (GMT-3)", "America/Paramaribo (GMT-3)": "America/Paramaribo (GMT-3)", "Atlantic/South_Georgia (GMT-2)": "Atlantic/South_Georgia (GMT-2)", "Atlantic/Azores (GMT-1)": "Atlantic/Azores (GMT-1; daylight GMT+0)", "Atlantic/Cape_Verde (GMT-1)": "Atlantic/Cape_Verde (GMT-1)", "UTC (GMT+0)": "UTC (GMT+0)", "Europe/London (GMT+0)": "Europe/London (GMT+0; daylight GMT+1)", "Europe/Dublin (GMT+0)": "Europe/Dublin (GMT+0; daylight GMT+1)", "Europe/Lisbon (GMT+0)": "Europe/Lisbon (GMT+0; daylight GMT+1)", "Africa/Casablanca (GMT+1)": "Africa/Casablanca (GMT+1)", "Africa/Accra (GMT+0)": "Africa/Accra (GMT+0)", "Atlantic/Reykjavik (GMT+0)": "Atlantic/Reykjavik (GMT+0)", "Europe/Paris (GMT+1)": "Europe/Paris (GMT+1; daylight GMT+2)", "Europe/Berlin (GMT+1)": "Europe/Berlin (GMT+1; daylight GMT+2)", "Europe/Rome (GMT+1)": "Europe/Rome (GMT+1; daylight GMT+2)", "Europe/Madrid (GMT+1)": "Europe/Madrid (GMT+1; daylight GMT+2)", "Europe/Amsterdam (GMT+1)": "Europe/Amsterdam (GMT+1; daylight GMT+2)", "Europe/Brussels (GMT+1)": "Europe/Brussels (GMT+1; daylight GMT+2)", "Europe/Vienna (GMT+1)": "Europe/Vienna (GMT+1; daylight GMT+2)", "Europe/Zurich (GMT+1)": "Europe/Zurich (GMT+1; daylight GMT+2)", "Europe/Stockholm (GMT+1)": "Europe/Stockholm (GMT+1; daylight GMT+2)", "Europe/Oslo (GMT+1)": "Europe/Oslo (GMT+1; daylight GMT+2)", "Europe/Copenhagen (GMT+1)": "Europe/Copenhagen (GMT+1; daylight GMT+2)", "Europe/Warsaw (GMT+1)": "Europe/Warsaw (GMT+1; daylight GMT+2)", "Europe/Prague (GMT+1)": "Europe/Prague (GMT+1; daylight GMT+2)", "Europe/Budapest (GMT+1)": "Europe/Budapest (GMT+1; daylight GMT+2)", "Europe/Belgrade (GMT+1)": "Europe/Belgrade (GMT+1; daylight GMT+2)", "Africa/Lagos (GMT+1)": "Africa/Lagos (GMT+1)", "Africa/Tunis (GMT+1)": "Africa/Tunis (GMT+1)", "Africa/Cairo (GMT+2)": "Africa/Cairo (GMT+2; daylight GMT+3)", "Europe/Athens (GMT+2)": "Europe/Athens (GMT+2; daylight GMT+3)", "Europe/Bucharest (GMT+2)": "Europe/Bucharest (GMT+2; daylight GMT+3)", "Europe/Helsinki (GMT+2)": "Europe/Helsinki (GMT+2; daylight GMT+3)", "Europe/Kyiv (GMT+2)": "Europe/Kyiv (GMT+2; daylight GMT+3)", "Europe/Istanbul (GMT+3)": "Europe/Istanbul (GMT+3)", "Africa/Johannesburg (GMT+2)": "Africa/Johannesburg (GMT+2)", "Africa/Nairobi (GMT+3)": "Africa/Nairobi (GMT+3)", "Asia/Jerusalem (GMT+2)": "Asia/Jerusalem (GMT+2; daylight GMT+3)", "Asia/Amman (GMT+3)": "Asia/Amman (GMT+3)", "Asia/Beirut (GMT+2)": "Asia/Beirut (GMT+2; daylight GMT+3)", "Europe/Moscow (GMT+3)": "Europe/Moscow (GMT+3)", "Asia/Baghdad (GMT+3)": "Asia/Baghdad (GMT+3)", "Asia/Riyadh (GMT+3)": "Asia/Riyadh (GMT+3)", "Asia/Kuwait (GMT+3)": "Asia/Kuwait (GMT+3)", "Asia/Qatar (GMT+3)": "Asia/Qatar (GMT+3)", "Africa/Addis_Ababa (GMT+3)": "Africa/Addis_Ababa (GMT+3)", "Asia/Tehran (GMT+3:30)": "Asia/Tehran (GMT+3:30)", "Asia/Dubai (GMT+4)": "Asia/Dubai (GMT+4)", "Asia/Muscat (GMT+4)": "Asia/Muscat (GMT+4)", "Asia/Baku (GMT+4)": "Asia/Baku (GMT+4)", "Asia/Tbilisi (GMT+4)": "Asia/Tbilisi (GMT+4)", "Indian/Mauritius (GMT+4)": "Indian/Mauritius (GMT+4)", "Asia/Kabul (GMT+4:30)": "Asia/Kabul (GMT+4:30)", "Asia/Karachi (GMT+5)": "Asia/Karachi (GMT+5)", "Asia/Tashkent (GMT+5)": "Asia/Tashkent (GMT+5)", "Asia/Yekaterinburg (GMT+5)": "Asia/Yekaterinburg (GMT+5)", "Asia/Kolkata (GMT+5:30)": "Asia/Kolkata (GMT+5:30)", "Asia/Colombo (GMT+5:30)": "Asia/Colombo (GMT+5:30)", "Asia/Kathmandu (GMT+5:45)": "Asia/Kathmandu (GMT+5:45)", "Asia/Dhaka (GMT+6)": "Asia/Dhaka (GMT+6)", "Asia/Almaty (GMT+5)": "Asia/Almaty (GMT+5)", "Asia/Rangoon (GMT+6:30)": "Asia/Rangoon (GMT+6:30)", "Asia/Bangkok (GMT+7)": "Asia/Bangkok (GMT+7)", "Asia/Jakarta (GMT+7)": "Asia/Jakarta (GMT+7)", "Asia/Ho_Chi_Minh (GMT+7)": "Asia/Ho_Chi_Minh (GMT+7)", "Asia/Singapore (GMT+8)": "Asia/Singapore (GMT+8)", "Asia/Kuala_Lumpur (GMT+8)": "Asia/Kuala_Lumpur (GMT+8)", "Asia/Shanghai (GMT+8)": "Asia/Shanghai (GMT+8)", "Asia/Hong_Kong (GMT+8)": "Asia/Hong_Kong (GMT+8)", "Asia/Taipei (GMT+8)": "Asia/Taipei (GMT+8)", "Asia/Manila (GMT+8)": "Asia/Manila (GMT+8)", "Australia/Perth (GMT+8)": "Australia/Perth (GMT+8)", "Asia/Tokyo (GMT+9)": "Asia/Tokyo (GMT+9)", "Asia/Seoul (GMT+9)": "Asia/Seoul (GMT+9)", "Asia/Pyongyang (GMT+9)": "Asia/Pyongyang (GMT+9)", "Australia/Adelaide (GMT+9:30)": "Australia/Adelaide (GMT+9:30; daylight GMT+10:30)", "Australia/Darwin (GMT+9:30)": "Australia/Darwin (GMT+9:30)", "Australia/Sydney (GMT+10)": "Australia/Sydney (GMT+10; daylight GMT+11)", "Australia/Melbourne (GMT+10)": "Australia/Melbourne (GMT+10; daylight GMT+11)", "Australia/Brisbane (GMT+10)": "Australia/Brisbane (GMT+10)", "Australia/Hobart (GMT+10)": "Australia/Hobart (GMT+10; daylight GMT+11)", "Pacific/Guam (GMT+10)": "Pacific/Guam (GMT+10)", "Pacific/Port_Moresby (GMT+10)": "Pacific/Port_Moresby (GMT+10)", "Asia/Vladivostok (GMT+10)": "Asia/Vladivostok (GMT+10)", "Pacific/Noumea (GMT+11)": "Pacific/Noumea (GMT+11)", "Pacific/Norfolk (GMT+11)": "Pacific/Norfolk (GMT+11; daylight GMT+12)", "Asia/Magadan (GMT+11)": "Asia/Magadan (GMT+11)", "Pacific/Auckland (GMT+12)": "Pacific/Auckland (GMT+12; daylight GMT+13)", "Pacific/Fiji (GMT+12)": "Pacific/Fiji (GMT+12)", "Pacific/Chatham (GMT+12:45)": "Pacific/Chatham (GMT+12:45; daylight GMT+13:45)", "Pacific/Tongatapu (GMT+13)": "Pacific/Tongatapu (GMT+13)", "Pacific/Apia (GMT+13)": "Pacific/Apia (GMT+13)", "Pacific/Kiritimati (GMT+14)": "Pacific/Kiritimati (GMT+14)" };
  var frameStatusPath = "/espframe/api/v1/status";
  var screenControlPath = "/espframe/api/v1/screen";
  var NativeSettingsApi = class extends EspframeApiClient {
    async getFrameStatus() {
      const capabilities = await this.negotiate();
      if ((capabilities == null ? void 0 : capabilities.status_path) !== frameStatusPath) return null;
      const payload = await this.requestJson(frameStatusPath, { cache: "no-store" }, "frame_status_failed");
      if (!payload || typeof payload !== "object") throw new Error("invalid_frame_status");
      const status = payload;
      const controlsScreen = capabilities.screen_control_path === screenControlPath;
      if (status.api_version !== capabilities.api_version || typeof status.restarting !== "boolean" || typeof status.screen !== "string" || typeof status.playback !== "string" || typeof status.source !== "string" || typeof status.immich !== "string" || typeof status.notice !== "string" || controlsScreen && typeof status.screen_pending !== "boolean") throw new Error("invalid_frame_status");
      return { ...status, screenPending: controlsScreen ? status.screen_pending : null };
    }
    async changeScreen(action) {
      const payload = await this.requestJson(screenControlPath, {
        method: "POST",
        cache: "no-store",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: new URLSearchParams({ action })
      }, "screen_control_failed");
      if (!payload || typeof payload !== "object" || payload.status !== "accepted")
        throw new Error("screen_control_failed");
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
    const screenButton = button("Sleep screen");
    screenButton.id = "native-screen-action";
    const actionStatus = el("span");
    actionStatus.setAttribute("role", "status");
    const controls = el("div", "native-screen-controls");
    controls.hidden = true;
    controls.append(screenButton, actionStatus);
    panel.append(values, notice, controls);
    let timer;
    let busy = false;
    let supported = true;
    let commanding = false;
    let commandVersion = 0;
    let latest = null;
    const setNotice = (message) => {
      if (notice.textContent !== message) notice.textContent = message;
      notice.hidden = !message;
    };
    const renderControl = () => {
      controls.hidden = !latest || latest.restarting || latest.screenPending === null || latest.screen !== "awake" && latest.screen !== "asleep";
      screenButton.disabled = commanding || (latest == null ? void 0 : latest.screenPending) === true;
      const label = (latest == null ? void 0 : latest.screen) === "asleep" ? "Wake screen" : "Sleep screen";
      if (screenButton.textContent !== label) screenButton.textContent = label;
    };
    const render = (status) => {
      latest = status;
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
      renderControl();
    };
    const scheduleRefresh = (delay = 5e3) => {
      window.clearTimeout(timer);
      if (supported && !document.hidden && !commanding) timer = window.setTimeout(refresh, delay);
    };
    const visibilityChanged = () => {
      window.clearTimeout(timer);
      if (!document.hidden) void refresh();
    };
    const refresh = async () => {
      if (document.hidden || busy || commanding || !supported) return;
      busy = true;
      const version = commandVersion;
      try {
        const status = await apiClient.getFrameStatus();
        if (version !== commandVersion) return;
        if (!status) {
          supported = false;
          panel.hidden = true;
          document.removeEventListener("visibilitychange", visibilityChanged);
          return;
        }
        render(status);
      } catch (_) {
        if (version !== commandVersion) return;
        latest = null;
        values.hidden = true;
        setNotice("The frame is unavailable. Waiting to reconnect.");
        renderControl();
      } finally {
        busy = false;
        scheduleRefresh();
      }
    };
    screenButton.onclick = async () => {
      if (commanding || screenButton.disabled || controls.hidden || !latest) return;
      const action = latest.screen === "asleep" ? "wake" : "sleep";
      const expected = action === "wake" ? "awake" : "asleep";
      commanding = true;
      commandVersion++;
      window.clearTimeout(timer);
      renderControl();
      actionStatus.textContent = action === "wake" ? "Waking screen\u2026" : "Putting screen to sleep\u2026";
      let confirmed = false;
      try {
        await apiClient.changeScreen(action);
        for (let attempt = 0; attempt < 10; ++attempt) {
          const status = await apiClient.getFrameStatus();
          if (!status || status.restarting || status.screenPending === null) break;
          render(status);
          if (!status.screenPending) {
            confirmed = status.screen === expected;
            break;
          }
          await delayMs(125);
        }
        actionStatus.textContent = confirmed ? "" : "Couldn\u2019t confirm the screen change.";
      } catch (error) {
        actionStatus.textContent = error instanceof EspframeApiError && error.kind === "conflict" ? "The frame is busy. Try again when it\u2019s ready." : "Couldn\u2019t confirm the screen change.";
      } finally {
        commanding = false;
        renderControl();
        scheduleRefresh(confirmed ? 5e3 : 0);
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
    ["Immich connection", [["immich_url", "Immich server URL"], ["api_key", "Replacement Immich API key"]]],
    ["Photo preparation service", [["prepared_slide_url", "Service URL"], ["prepared_slide_token", "Service access token"]]],
    ["Playback", [
      ["interval", "Time per photo"],
      ["photo_orientation", "Display photos"],
      ["display_mode", "Photo framing"],
      ["portrait_pairing", "Pair portrait photos"],
      ["portrait_pair_framing", "Portrait pair framing"],
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
      ["screen_rotation", "Mounting orientation"],
      ["brightness_day", "Daytime brightness (%)"],
      ["brightness_night", "Nighttime brightness (%)"],
      ["base_tone_enabled", "Adjust display tone"],
      ["base_tone", "Display warmth (%)"],
      ["warm_tones_enabled", "Use warmer tones at night"],
      ["warm_tone_intensity", "Nighttime warmth (%)"]
    ]],
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
  function nativeSettingControl(key, value) {
    var _a, _b;
    const spec = PRODUCT_SETTINGS[key];
    const options = key === "timezone" ? TIMEZONES.slice() : key === "portrait_pair_framing" ? ["Follow photo framing", "Fill", "Fit"] : (_b = (_a = spec == null ? void 0 : spec.options) == null ? void 0 : _a.slice()) != null ? _b : [];
    if (options.length) {
      const current = String(value);
      if (!options.includes(current)) options.push(current);
      return selectFromOptions(options, current, function() {
      }, function(option) {
        if (key === "timezone") return (TIMEZONE_LABELS[option] || option).replace(/_/g, " ");
        if (key === "display_mode") return option === "Fit" ? "Show full image" : "Crop to fit";
        if (key === "portrait_pair_framing" && option !== "Follow photo framing") {
          return option === "Fill" ? "Fill each half" : "Fit both full photos";
        }
        if (key === "screen_rotation") return option === "180" ? "Upside down (180\xB0)" : "Normal";
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
  function nativeSettingsForm(snapshot) {
    var _a, _b, _c, _d, _e, _f;
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
        if (unavailable.has(key) || key !== "api_key" && !Object.prototype.hasOwnProperty.call(snapshot.values, snapshotKey)) continue;
        const value = nativeSecret(key) ? "" : snapshot.values[key];
        const control = nativeSettingControl(key, value);
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
          if (key === "prepared_slide_url") hint.textContent = "Optional. Clear the URL to use direct Immich previews exclusively.";
          if (key === "prepared_slide_token") hint.textContent = snapshot.values.prepared_slide_token_configured ? "Leave blank to keep the current token. Enter a new token if you change the service URL." : "Enter the token from your preparation service.";
          control.setAttribute("aria-describedby", hint.id);
          row.append(control, hint);
        } else {
          row.appendChild(control);
        }
        controls.set(key, { element: control, value });
        body.appendChild(row);
      }
      const openConnection = title === "Immich connection" && !snapshot.api_key_configured;
      if (body.childElementCount) fields.appendChild(makeCollapsibleCard(title, body, title !== "Playback" && !openConnection));
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
      (_d = controls.get("portrait_pair_framing")) == null ? void 0 : _d.element
    ];
    const preparedUrl = controls.get("prepared_slide_url");
    const preparedToken = (_e = controls.get("prepared_slide_token")) == null ? void 0 : _e.element;
    const nameInput = (_f = controls.get("frame_name")) == null ? void 0 : _f.element;
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
      try {
        const response = await apiClient.updateSettings(values, []);
        if (!response || response.status !== "accepted") throw new Error("save_failed");
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
