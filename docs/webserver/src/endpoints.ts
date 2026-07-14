  function eid(domain, name) {
    return "/" + domain + "/" + encodeURIComponent(name);
  }

  function entityStringParts(entity) {
    entity = typeof entity === "string" ? entity : "";
    var slash = entity.indexOf("/");
    if (slash > 0) {
      return {
        domain: entity.slice(0, slash),
        name: entity.slice(slash + 1)
      };
    }
    return null;
  }

  function productSettingEntityParts(key) {
    var spec = PRODUCT_SETTINGS && PRODUCT_SETTINGS[key];
    return entityStringParts(spec && spec.entity);
  }

  function settingEntityParts(key) {
    var parts = productSettingEntityParts(key);
    if (!parts && STATIC_ENTITIES && STATIC_ENTITIES[key]) {
      parts = entityStringParts(STATIC_ENTITIES[key].entity);
    }
    if (!parts && MANUAL_ENTITIES && MANUAL_ENTITIES[key]) {
      parts = entityStringParts(MANUAL_ENTITIES[key].entity);
    }
    return parts;
  }

  function settingEntityDomain(key) {
    var parts = settingEntityParts(key);
    return parts && parts.domain ? parts.domain : "";
  }

  var endpoints = {};
  var CONFIGURATION_API_PATH = "/espframe/api/v1/configuration";
  var configurationUpdateQueue = Promise.resolve();

  function configurationApiUnavailable(message) {
    var error = new Error(message || "configuration_api_unavailable");
    error.configurationApiUnavailable = true;
    return error;
  }

  function isConfigurationApiUnavailable(error) {
    return !!(error && error.configurationApiUnavailable);
  }

  function getConfigurationSnapshot() {
    return fetch(CONFIGURATION_API_PATH)
      .then(function (response) {
        if (!response.ok) throw configurationApiUnavailable("configuration_api_" + response.status);
        return response.json();
      })
      .then(function (payload) {
        var snapshot = parseConfigurationSnapshot(payload);
        if (!snapshot) throw configurationApiUnavailable("invalid_configuration_snapshot");
        return snapshot;
      })
      .catch(function (error) {
        if (isConfigurationApiUnavailable(error)) throw error;
        throw configurationApiUnavailable("configuration_api_request_failed");
      });
  }

  function sendConfigurationUpdate(values) {
    var encoded = configurationUpdateBody(values);
    return fetch(CONFIGURATION_API_PATH, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: encoded
    }).then(function (response) {
      if (response.status === 404 || response.status === 405) {
        throw configurationApiUnavailable("configuration_api_" + response.status);
      }
      return response.json().catch(function () { return null; }).then(function (payload) {
        if (!response.ok || !payload || payload.status !== "accepted") {
          var error = new Error(payload && payload.error ? payload.error : "configuration_update_failed");
          error.field = payload && payload.field;
          error.configurationApiResponse = true;
          throw error;
        }
        return delayMs(100).then(function () { return payload; });
      });
    }).catch(function (error) {
      if (isConfigurationApiUnavailable(error) || error.configurationApiResponse) {
        throw error;
      }
      throw configurationApiUnavailable("configuration_api_request_failed");
    });
  }

  function updateConfiguration(values) {
    var request = configurationUpdateQueue.then(function () {
      return sendConfigurationUpdate(values);
    });
    configurationUpdateQueue = request.catch(function () { return null; });
    return request;
  }

  function applyConfigurationSnapshot(snapshot) {
    Object.keys(snapshot.values).forEach(function (key) {
      S[key] = snapshot.values[key];
    });
  }

  function registerManualEntityEndpoints() {
    if (!MANUAL_ENTITIES) return;
    Object.keys(MANUAL_ENTITIES).forEach(function (key) {
      var parts = entityStringParts(MANUAL_ENTITIES[key] && MANUAL_ENTITIES[key].entity);
      if (!parts) return;
      endpoints[key] = eid(parts.domain, parts.name);
    });
  }

  function registerStaticEntityEndpoints() {
    if (!STATIC_ENTITIES) return;
    Object.keys(STATIC_ENTITIES).forEach(function (key) {
      var parts = entityStringParts(STATIC_ENTITIES[key] && STATIC_ENTITIES[key].entity);
      if (!parts) return;
      endpoints[key] = eid(parts.domain, parts.name);
    });
  }

  function registerProductSettingEndpoints() {
    if (!PRODUCT_SETTINGS) return;
    Object.keys(PRODUCT_SETTINGS).forEach(function (key) {
      var parts = productSettingEntityParts(key);
      if (!parts) return;
      endpoints[key] = eid(parts.domain, parts.name);
    });
  }

  registerManualEntityEndpoints();
  registerStaticEntityEndpoints();
  registerProductSettingEndpoints();

  function post(url, params) {
    var fullUrl = params ? url + "?" + new URLSearchParams(params).toString() : url;
    return fetch(fullUrl, { method: "POST" }).then(function (r) {
      if (!r.ok) {
        console.error("POST " + fullUrl + " failed: " + r.status);
        throw new Error("post_failed");
      }
      return r;
    }).catch(function (err) {
      console.error("POST " + fullUrl + " error:", err);
      showBanner("Failed to save setting", "error");
      throw err;
    });
  }
