  function renderWizard() {
    var step = 1;
    immichApp.replaceChildren();
    app.replaceChildren();
    renderStartupDevicePage();
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
    immichApp.appendChild(wrap);

    function showStep() {
      body.replaceChildren();
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

      var urlField = makeConnectionUrlField(S.immich_url);
      var urlInput = urlField.input;
      card.appendChild(urlField.field);

      var f2 = field("API Key");
      var keyControl = makeApiKeyInputGroup({
        type: "password",
        value: S.api_key,
        placeholder: "Your Immich API key",
        toggleVisibility: true
      });
      var keyInput = keyControl.input;
      f2.appendChild(keyControl.group);
      card.appendChild(f2);

      var nav = el("div", "wizard-nav");
      var nextBtn = el("button", "btn btn-primary");
      nextBtn.textContent = "Connect";
      nextBtn.onclick = function () {
        var u = normalizeImmichUrl(urlInput.value);
        var k = keyInput.value.trim();
        if (!u || !k) return;
        nextBtn.disabled = true;
        nextBtn.textContent = "Saving\u2026";
        saveAndVerifyConnection(u, k)
          .then(function (connection) {
            urlInput.value = connection.url;
            step = 2;
            showStep();
          })
          .catch(function () {
            nextBtn.disabled = false;
            nextBtn.textContent = "Connect";
            showBanner("Failed to save connection. Please try again.", "error");
          });
      };
      nav.appendChild(nextBtn);
      card.appendChild(nav);
      return card;
    }

    function renderStep2() {
      var card = el("div", "card fade-in");
      card.innerHTML = "<h3>Clock & timezone</h3>";

      var f1 = field("Clock Format");
      f1.appendChild(
        selectFromOptions(productSettingOptions("clock_format"), S.clock_format, function (v) {
          saveSetting("clock_format", v);
        })
      );
      card.appendChild(f1);

      var f2 = field("Timezone");
      f2.appendChild(
        timezoneSelect(S.tz_options, S.timezone, function (v) {
          saveSetting("timezone", v);
        })
      );
      card.appendChild(f2);

      card.appendChild(ntpServersField());

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

  function renderStartupDevicePage() {
    var wrap = el("div", "fade-in");
    wrap.appendChild(makeImportSettingsCard());
    app.appendChild(wrap);
  }

  // --- Settings ---
