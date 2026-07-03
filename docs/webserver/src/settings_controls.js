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
      makeWifiCard: makeWifiCard,
      makeDeveloperCard: makeDeveloperCard,
      makeBackupCard: makeBackupCard
    };
  }

  function renderSettingsCardsForTab(tabId) {
    var renderers = settingsCardRenderers();
    if (!Array.isArray(WEB_UI_CARDS) || !WEB_UI_CARDS.length) return [];
    return WEB_UI_CARDS.filter(function (card) {
      return card && card.tab === tabId;
    }).map(function (card) {
      var renderer = renderers[card.function];
      return renderer ? renderer() : null;
    });
  }

  function renderSettings() {
    app.innerHTML = "";
    immichApp.innerHTML = "";
    var immichWrap = el("div", "fade-in");
    var wrap = el("div", "fade-in");

    var immichCards = renderSettingsCardsForTab("immich");
    var settingsCards = renderSettingsCardsForTab("settings");
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

    if (!settingsCards.length) settingsCards = [
      makeScreenBrightnessCard(),
      makeScreenToneCard(),
      makeNightScheduleCard(),
      makeRotationCard(),
      makeClockCard(),
      makeFirmwareCard(),
      makeWifiCard(),
      makeDeveloperCard(),
      makeBackupCard()
    ];
    appendCards(wrap, settingsCards);
    app.appendChild(wrap);
  }
