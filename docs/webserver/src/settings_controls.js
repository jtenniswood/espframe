  function appendCards(parent, cards) {
    cards.forEach(function (card) {
      if (card) parent.appendChild(card);
    });
  }

  function renderSettings() {
    app.innerHTML = "";
    immichApp.innerHTML = "";
    var immichWrap = el("div", "fade-in");
    var wrap = el("div", "fade-in");

    appendCards(immichWrap, [
      makeConnectionCard(),
      makeFrequencyCard(),
      makePhotoSourceCard(),
      makeAdvancedFiltersCard(),
      makeLayoutCard(),
      makeMetadataCard()
    ]);
    immichApp.appendChild(immichWrap);

    appendCards(wrap, [
      makeScreenBrightnessCard(),
      makeScreenToneCard(),
      makeNightScheduleCard(),
      makeRotationCard(),
      makeClockCard(),
      makeFirmwareCard(),
      makeDeveloperCard(),
      makeBackupCard(),
      makeDeviceRebootCard()
    ]);
    app.appendChild(wrap);
  }
