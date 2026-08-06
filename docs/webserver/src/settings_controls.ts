  function appendCards(parent, cards) {
    cards.forEach(function (card) {
      if (card) parent.appendChild(card);
    });
  }

  function settingsCardRenderers() {
    return {
      makeConnectionCard: makeConnectionCard,
      makeFrequencyCard: makeFrequencyCard,
      makePhotoSourceCard: makePhotoSourceCard,
      makeAdvancedFiltersCard: makeAdvancedFiltersCard,
      makeLayoutCard: makeLayoutCard,
      makeMetadataCard: makeMetadataCard,
      makeScreenBrightnessCard: makeScreenBrightnessCard,
      makeScreenToneCard: makeScreenToneCard,
      makeNightScheduleCard: makeNightScheduleCard,
      makeRotationCard: makeRotationCard,
      makeClockCard: makeClockCard,
      makeFirmwareCard: makeFirmwareCard,
      makeDeviceRebootCard: makeDeviceRebootCard,
      makeDeveloperCard: makeDeveloperCard,
      makeBackupCard: makeBackupCard
    };
  }

  function renderSettingsCardsForTab(tabId) {
    return renderSettingsCardEntriesForTab(tabId).map(function (entry) { return entry.element; });
  }

  function renderSettingsCardEntriesForTab(tabId) {
    var renderers = settingsCardRenderers();
    if (!Array.isArray(WEB_UI_CARDS) || !WEB_UI_CARDS.length) return [];
    return WEB_UI_CARDS.filter(function (card) {
      return card && card.tab === tabId;
    }).map(function (card) {
      var renderer = renderers[card.function];
      return renderer ? { section: card.section || "", element: renderer() } : null;
    }).filter(function (entry) { return entry && entry.element; });
  }

  function appendSettingsSections(parent, entries) {
    var sections = {};
    entries.forEach(function (entry) {
      if (!entry || !entry.element) return;
      var sectionName = String(entry.section || "").trim();
      if (!sectionName) {
        parent.appendChild(entry.element);
        return;
      }
      if (!sections[sectionName]) {
        var section = el("section", "settings-section");
        var heading = document.createElement("h2");
        heading.className = "settings-section-title";
        heading.id = "settings-section-" + sectionName.toLowerCase().replace(/[^a-z0-9]+/g, "-");
        heading.textContent = sectionName;
        section.setAttribute("aria-labelledby", heading.id);
        section.appendChild(heading);
        sections[sectionName] = section;
        parent.appendChild(section);
      }
      sections[sectionName].appendChild(entry.element);
    });
  }

  function renderSettings() {
    app.replaceChildren();
    immichApp.replaceChildren();
    var immichWrap = el("div", "fade-in");
    var wrap = el("div", "fade-in");

    var immichCards = renderSettingsCardsForTab("immich");
    var settingsCardEntries = renderSettingsCardEntriesForTab("settings");
    if (!immichCards.length) immichCards = [
      makeConnectionCard(),
      makeFrequencyCard(),
      makePhotoSourceCard(),
      makeAdvancedFiltersCard(),
      makeLayoutCard(),
      makeMetadataCard()
    ];
    appendCards(immichWrap, immichCards);
    immichApp.appendChild(immichWrap);

    if (!settingsCardEntries.length) settingsCardEntries = [
      { section: "Display", element: makeScreenBrightnessCard() },
      { section: "Display", element: makeScreenToneCard() },
      { section: "Display", element: makeRotationCard() },
      { section: "Display", element: makeClockCard() },
      { section: "Sleep & Schedule", element: makeNightScheduleCard() },
      { section: "System", element: makeBackupCard() },
      { section: "System", element: makeFirmwareCard() },
      { section: "System", element: makeDeviceRebootCard() },
      { section: "System", element: makeDeveloperCard() }
    ];
    appendSettingsSections(wrap, settingsCardEntries);
    app.appendChild(wrap);
  }
