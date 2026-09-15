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

  var endpoints: Record<string, string> = {};
  function getConfigurationSnapshot() {
    return apiClient.getConfigurationSnapshot();
  }

  function applyConfigurationSnapshot(snapshot) {
    settingSaves.receive("api_key_configured", snapshot.api_key_configured);
    Object.keys(snapshot.values).forEach(function (key) {
      settingSaves.receive(key, snapshot.values[key]);
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

  function post(url, params?) {
    return apiClient.post(url, params).catch(function (err) {
      console.error("POST " + url + " error:", err);
      showBanner("Failed to save setting", "error");
      throw err;
    });
  }
