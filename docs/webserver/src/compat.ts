export const MAX_PHOTO_ID_FIELD_LENGTH = 255;

  function normalizeNtpServer(value) {
    return String(value == null ? "" : value).trim();
  }

  // WireGuard setting checks. These mirror components/espframe/wireguard_helpers.h
  // so the web UI rejects the same values the firmware would.
  var MAX_WIREGUARD_ALLOWED_IPS = 4;

  function parseIpv4(value) {
    var match = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(String(value == null ? "" : value));
    if (!match) return null;
    var result = 0;
    for (var i = 1; i <= 4; i++) {
      var octet = parseInt(match[i], 10);
      if (octet > 255) return null;
      result = result * 256 + octet;
    }
    return result;
  }

  function formatIpv4(address) {
    return [Math.floor(address / 16777216) % 256, Math.floor(address / 65536) % 256,
      Math.floor(address / 256) % 256, address % 256].join(".");
  }

  // "12s ago", "3 min ago", "2 h ago", or "" before the first handshake.
  function formatWireguardHandshakeAge(seconds) {
    var value = Number(seconds);
    if (seconds == null || seconds === "" || !isFinite(value) || value < 0) return "";
    value = Math.round(value);
    if (value < 120) return value + "s ago";
    if (value < 7200) return Math.floor(value / 60) + " min ago";
    return Math.floor(value / 3600) + " h ago";
  }

  function isValidWireguardKey(value) {
    return /^[A-Za-z0-9+/]{42}[AEIMQUYcgkosw480]=$/.test(String(value == null ? "" : value).trim());
  }

  function isValidWireguardAddress(value) {
    var address = parseIpv4(String(value == null ? "" : value).trim());
    return address !== null && address !== 0 && address !== 4294967295;
  }

  function isValidIpv4Netmask(value) {
    var mask = parseIpv4(String(value == null ? "" : value).trim());
    if (mask === null) return false;
    var inverted = 4294967295 - mask;
    return ((inverted + 1) & inverted) === 0;
  }

  function isValidWireguardPort(value) {
    var text = String(value == null ? "" : value).trim();
    if (!/^\d{1,5}$/.test(text)) return false;
    var port = parseInt(text, 10);
    return port >= 1 && port <= 65535;
  }

  function isValidWireguardEndpoint(value) {
    var endpoint = String(value == null ? "" : value).trim();
    if (!endpoint || endpoint.length > 253) return false;
    return /^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$/.test(endpoint) && endpoint.indexOf("..") === -1;
  }

  // Canonical "a.b.c.d/len, ..." list, or "" when any entry is invalid, the list
  // is empty, or it has more entries than the firmware accepts.
  function normalizeWireguardAllowedIps(value) {
    var tokens = String(value == null ? "" : value).split(/[\s,;]+/).filter(Boolean);
    if (!tokens.length || tokens.length > MAX_WIREGUARD_ALLOWED_IPS) return "";
    var result = [];
    for (var i = 0; i < tokens.length; i++) {
      var parts = tokens[i].split("/");
      if (parts.length > 2) return "";
      var address = parseIpv4(parts[0]);
      if (address === null) return "";
      var prefix = 32;
      if (parts.length === 2) {
        if (!/^\d{1,2}$/.test(parts[1])) return "";
        prefix = parseInt(parts[1], 10);
        if (prefix > 32) return "";
      }
      var hostBits = Math.pow(2, 32 - prefix);
      result.push(formatIpv4(Math.floor(address / hostBits) * hostBits) + "/" + prefix);
    }
    return result.join(", ");
  }

  var LEGACY_DATE_TAKEN_FORMATS = {
    "January 1, 2000": "January 1, 2026",
    "January 1, 2026": "January 1, 2026",
    "Month Day, Year": "January 1, 2026",
    "Month Day Ordinal, Year": "January 1, 2026"
  };

  function normalizeDateTakenFormat(value) {
    return LEGACY_DATE_TAKEN_FORMATS[value] || "1 January, 2026";
  }

  function stripUrlTrailingSlashes(value) {
    var url = String(value == null ? "" : value);
    while (url.length > 0 && url.charAt(url.length - 1) === "/" && !/^[a-z][a-z0-9+.-]*:\/\/$/i.test(url)) {
      url = url.slice(0, -1);
    }
    return url;
  }

  function isValidHttpUrl(value) {
    try {
      var url = new URL(value);
      return (url.protocol === "http:" || url.protocol === "https:") && !!url.hostname;
    } catch (_) {
      return false;
    }
  }

  function extractUrlAuthority(value) {
    var url = String(value || "");
    if (url.indexOf("//") === 0) url = url.slice(2);
    return url.split(/[/?#]/)[0] || "";
  }

  function extractUrlHost(value) {
    var authority = extractUrlAuthority(value);
    var at = authority.lastIndexOf("@");
    if (at >= 0) authority = authority.slice(at + 1);
    if (!authority) return "";
    if (authority.charAt(0) === "[") {
      var close = authority.indexOf("]");
      return (close >= 0 ? authority.slice(0, close + 1) : authority).toLowerCase();
    }
    return authority.split(":")[0].toLowerCase();
  }

  function extractUrlPort(value) {
    var authority = extractUrlAuthority(value);
    var at = authority.lastIndexOf("@");
    if (at >= 0) authority = authority.slice(at + 1);
    if (!authority) return "";
    if (authority.charAt(0) === "[") {
      var close = authority.indexOf("]");
      if (close >= 0 && authority.charAt(close + 1) === ":") return authority.slice(close + 2).match(/^\d*/)[0];
      return "";
    }
    var colon = authority.indexOf(":");
    return colon >= 0 ? authority.slice(colon + 1).match(/^\d*/)[0] : "";
  }

  function urlHasExplicitPort(value) {
    return extractUrlPort(value) !== "";
  }

  function isLocalImmichHost(host) {
    if (!host) return false;
    if (host === "localhost" || host.charAt(0) === "[") return true;
    if (/^\d{1,3}(\.\d{1,3}){3}$/.test(host)) return true;
    return host.slice(-6) === ".local" || host.slice(-4) === ".lan";
  }

  function normalizeImmichUrl(value) {
    var url = stripUrlTrailingSlashes(String(value == null ? "" : value).trim());
    if (!url) return "";
    if (url.indexOf("//") === 0) {
      url = "https:" + url;
    } else if (!/^[a-z][a-z0-9+.-]*:\/\//i.test(url)) {
      var host = extractUrlHost(url);
      var port = extractUrlPort(url);
      var useHttp = isLocalImmichHost(host) || urlHasExplicitPort(url);
      if (port === "443") useHttp = false;
      url = (useHttp ? "http://" : "https://") + url;
    }
    return stripUrlTrailingSlashes(url.replace(/^([a-z][a-z0-9+.-]*):\/\//i, function (_, scheme) {
      return scheme.toLowerCase() + "://";
    }));
  }

  function photoIdFieldLengthLimit() {
    return MAX_PHOTO_ID_FIELD_LENGTH;
  }

  function photoIdFieldTooLong(s) {
    return String(s != null ? s : "").trim().length > photoIdFieldLengthLimit();
  }

  function photoLabelFieldTooLong(s) {
    return String(s != null ? s : "").trim().length > photoIdFieldLengthLimit();
  }

  var UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
  function isValidUuidList(str) {
    var s = str.trim();
    if (!s) return true;
    return s.split(",").every(function (id) { return UUID_RE.test(id.trim()); });
  }

  function splitPhotoIdList(str) {
    var parts = String(str || "").split(",").map(function (id) {
      return id.trim();
    }).filter(Boolean);
    return parts.length ? parts : [""];
  }

  function parsePhotoLabelList(str) {
    var raw = String(str || "").trim();
    if (!raw) return [];
    try {
      var parsed = JSON.parse(raw);
      if (Array.isArray(parsed)) return parsed.map(function (label) { return String(label || ""); });
    } catch (_) {}
    return raw.split(",").map(function (label) { return label.trim(); });
  }

export { formatWireguardHandshakeAge, isValidWireguardKey, isValidWireguardAddress, isValidIpv4Netmask, isValidWireguardPort, isValidWireguardEndpoint, normalizeWireguardAllowedIps, extractUrlHost, extractUrlPort, isValidHttpUrl, normalizeImmichUrl, normalizeNtpServer, normalizeDateTakenFormat, parsePhotoLabelList, photoIdFieldTooLong, photoLabelFieldTooLong, splitPhotoIdList, isValidUuidList };
