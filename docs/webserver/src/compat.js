  function normalizeNtpServer(value) {
    return String(value == null ? "" : value).trim();
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

  function normalizeFirmwareManifestUrl(value) {
    return stripUrlTrailingSlashes(String(value == null ? "" : value).trim());
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
    return typeof MAX_PHOTO_ID_FIELD_LENGTH !== "undefined" ? MAX_PHOTO_ID_FIELD_LENGTH : 255;
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

if (typeof module !== "undefined") {
  module.exports = {
    extractUrlHost: extractUrlHost,
    extractUrlPort: extractUrlPort,
    isValidHttpUrl: isValidHttpUrl,
    normalizeFirmwareManifestUrl: normalizeFirmwareManifestUrl,
    normalizeImmichUrl: normalizeImmichUrl,
    normalizeNtpServer: normalizeNtpServer,
    normalizeDateTakenFormat: normalizeDateTakenFormat,
    parsePhotoLabelList: parsePhotoLabelList,
    photoIdFieldTooLong: photoIdFieldTooLong,
    photoLabelFieldTooLong: photoLabelFieldTooLong,
    splitPhotoIdList: splitPhotoIdList,
    isValidUuidList: isValidUuidList
  };
}
