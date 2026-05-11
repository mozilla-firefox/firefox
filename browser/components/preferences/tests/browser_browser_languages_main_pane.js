/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests for the browser language section on the main preferences pane.
// Each test scenario runs against both the legacy groupbox UI and the new
// SRD (Settings Redesign) setting-group UI.

requestLongerTimeout(3);

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

AddonTestUtils.initMochitest(this);

function langpackId(locale) {
  return `langpack-${locale}@firefox.mozilla.org`;
}

function createLangpack(locale) {
  return AddonTestUtils.createTempXPIFile({
    "manifest.json": {
      langpack_id: locale,
      name: `${locale} Language Pack`,
      description: `${locale} Language pack`,
      languages: {
        [locale]: {
          chrome_resources: {
            branding: `browser/chrome/${locale}/locale/branding/`,
          },
          version: "1",
        },
      },
      browser_specific_settings: {
        gecko: {
          id: langpackId(locale),
          strict_min_version: AppConstants.MOZ_APP_VERSION,
          strict_max_version: AppConstants.MOZ_APP_VERSION,
        },
      },
      version: "2.0",
      manifest_version: 2,
      sources: {
        browser: {
          base_path: "browser/",
        },
      },
      author: "Mozilla",
    },
    [`browser/${locale}/branding/brand.ftl`]: "-brand-short-name = Firefox",
  });
}

async function installLangpack(locale) {
  let xpi = createLangpack(locale);
  let install = await AddonTestUtils.promiseInstallFile(xpi);
  return install.addon;
}

async function openPrefs() {
  await openPreferencesViaOpenPreferencesAPI("paneLanguages", {
    leaveOpen: true,
  });
  return gBrowser.contentDocument;
}

// --- UI helpers: abstract the differences between legacy and SRD UIs ---

async function waitForLanguageUI(doc, redesignEnabled) {
  if (!redesignEnabled) {
    let box = doc.getElementById("browserLanguagesBox");
    if (box.hidden) {
      await BrowserTestUtils.waitForMutationCondition(
        box,
        { attributes: true, attributeFilter: ["hidden"] },
        () => !box.hidden
      );
    }
  } else {
    let win = doc.defaultView;
    let sc = getSettingControl("browserLanguagePreferred", win);
    if (!sc?.controlEl?.children?.length) {
      await waitForSettingControlChange(sc);
    }
  }
}

async function waitForSettingVisible(settingId, win) {
  let sc = getSettingControl(settingId, win);
  if (!sc.hidden) {
    return;
  }
  await BrowserTestUtils.waitForMutationCondition(
    sc,
    { attributes: true, attributeFilter: ["hidden"] },
    () => !sc.hidden
  );
}

function getAvailableLocales(doc, redesignEnabled) {
  if (!redesignEnabled) {
    return Array.from(
      doc.getElementById("primaryBrowserLocale").querySelector("menupopup")
        .children
    ).map(item => item.value);
  }
  let sc = getSettingControl("browserLanguagePreferred", doc.defaultView);
  return Array.from(sc.controlEl.children).map(opt => opt.value);
}

async function changeLocale(doc, locale, redesignEnabled) {
  if (!redesignEnabled) {
    let menulist = doc.getElementById("primaryBrowserLocale");
    let menupopup = menulist.querySelector("menupopup");
    let item = menupopup.querySelector(`[value="${locale}"]`);
    ok(item, `Found menuitem for locale "${locale}"`);
    // SelectionChangedMenulist fires the handler on popuphiding, so dispatch
    // "command" first to set lastEvent, then "popuphiding" to invoke it.
    item.dispatchEvent(new Event("command", { bubbles: true }));
    menupopup.dispatchEvent(new Event("popuphiding"));
  } else {
    let sc = getSettingControl("browserLanguagePreferred", doc.defaultView);
    await changeMozSelectValue(sc.controlEl, locale);
  }
}

async function waitForRestartMessage(doc, redesignEnabled) {
  if (!redesignEnabled) {
    let messageBar = doc.getElementById("confirmBrowserLanguage");
    await BrowserTestUtils.waitForMutationCondition(
      messageBar,
      { attributes: true, attributeFilter: ["hidden"] },
      () => !messageBar.hidden
    );
  } else {
    let restartControl = getSettingControl(
      "browserLanguageMessage",
      doc.defaultView
    );
    await BrowserTestUtils.waitForMutationCondition(
      restartControl,
      { attributes: true, attributeFilter: ["hidden"] },
      () => !restartControl.hidden
    );
  }
}

function assertRestartMessageHidden(doc, redesignEnabled) {
  if (!redesignEnabled) {
    is(
      doc.getElementById("confirmBrowserLanguage").hidden,
      true,
      "Legacy restart message bar is hidden"
    );
  } else {
    let restartControl = getSettingControl(
      "browserLanguageMessage",
      doc.defaultView
    );
    is(restartControl.hidden, true, "SRD restart control is hidden");
  }
}

// --- Tests ---

// Installed langpacks should appear in the language selector.
add_task(async function testLangpacksAppearInMainPane() {
  for (let redesignEnabled of [false, true]) {
    info(
      `Testing langpack visibility with SRD ${redesignEnabled ? "enabled" : "disabled"}`
    );

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.settings-redesign.enabled", redesignEnabled],
        ["intl.multilingual.enabled", true],
        ["intl.multilingual.downloadEnabled", false],
        ["extensions.langpacks.signatures.required", false],
      ],
    });

    // Install "pl" then "fr" so they need to be sorted.
    let addons = await Promise.all(["pl", "fr"].map(installLangpack));
    let doc = await openPrefs();
    await waitForLanguageUI(doc, redesignEnabled);

    let locales = getAvailableLocales(doc, redesignEnabled);
    Assert.deepEqual(
      locales,
      ["en-US", "fr", "pl"],
      "Installed locales are listed and sorted"
    );

    await Promise.all(addons.map(addon => addon.uninstall()));
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});

// Selecting a different language when liveReload is off shows a restart
// confirmation.
add_task(async function testLanguageChangeShowsRestartConfirmation() {
  for (let redesignEnabled of [false, true]) {
    info(
      `Testing restart confirmation with SRD ${redesignEnabled ? "enabled" : "disabled"}`
    );

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.settings-redesign.enabled", redesignEnabled],
        ["intl.multilingual.enabled", true],
        ["intl.multilingual.downloadEnabled", false],
        ["intl.multilingual.liveReload", false],
        ["intl.multilingual.liveReloadBidirectional", false],
        ["intl.locale.requested", "en-US"],
        ["extensions.langpacks.signatures.required", false],
      ],
    });

    let addon = await installLangpack("fr");
    let doc = await openPrefs();
    await waitForLanguageUI(doc, redesignEnabled);

    assertRestartMessageHidden(doc, redesignEnabled);

    await changeLocale(doc, "fr", redesignEnabled);
    await waitForRestartMessage(doc, redesignEnabled);

    if (!redesignEnabled) {
      let button = doc
        .getElementById("confirmBrowserLanguage")
        .querySelector("button");
      ok(
        button.getAttribute("locales").startsWith("fr"),
        "Restart button encodes the new locale"
      );
    }

    await addon.uninstall();
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});

// Selecting a different language when liveReload is on applies it immediately
// without showing a restart confirmation.
add_task(async function testLanguageChangeLiveReload() {
  for (let redesignEnabled of [false, true]) {
    info(
      `Testing live reload with SRD ${redesignEnabled ? "enabled" : "disabled"}`
    );

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.settings-redesign.enabled", redesignEnabled],
        ["intl.multilingual.enabled", true],
        ["intl.multilingual.downloadEnabled", false],
        ["intl.multilingual.liveReload", true],
        ["intl.multilingual.liveReloadBidirectional", true],
        ["intl.locale.requested", "en-US"],
        ["extensions.langpacks.signatures.required", false],
      ],
    });

    let addon = await installLangpack("fr");
    let doc = await openPrefs();
    await waitForLanguageUI(doc, redesignEnabled);

    await changeLocale(doc, "fr", redesignEnabled);

    await BrowserTestUtils.waitForCondition(
      () => Services.locale.requestedLocales.includes("fr"),
      "The fr locale is applied immediately with live reload"
    );
    assertRestartMessageHidden(doc, redesignEnabled);

    await addon.uninstall();
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});

// --- Remote locale download tests (SRD only, downloadEnabled=true) ---

const { LangPackMatcher } = ChromeUtils.importESModule(
  "resource://gre/modules/LangPackMatcher.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

function createRemoteLangpack(locale) {
  return {
    target_locale: locale,
    hash: locale,
    url: `http://mochi.test:8888/${locale}.xpi`,
  };
}

async function waitForRemoteSeparator(win) {
  let sc = getSettingControl("browserLanguagePreferred", win);
  if (Array.from(sc.controlEl.children).some(el => el.localName === "hr")) {
    return;
  }
  // Wait for the preferred setting to refresh after remote locales arrive.
  await waitForSettingControlChange(sc);
}

// Installed locales should populate immediately even when the remote locale
// fetch is still pending.
add_task(async function testInstalledLocalesWhileRemotePending() {
  let sandbox = sinon.createSandbox();
  let resolveRemote;
  let remotePromise = new Promise(resolve => {
    resolveRemote = resolve;
  });
  sandbox
    .stub(LangPackMatcher.mockable, "getAvailableLangpacks")
    .callsFake(() => remotePromise);

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.settings-redesign.enabled", true],
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
    ],
  });

  let doc = await openPrefs();
  let win = doc.defaultView;
  await waitForLanguageUI(doc, true);

  let sc = getSettingControl("browserLanguagePreferred", win);
  let children = Array.from(sc.controlEl.children);
  ok(
    children.some(el => el.value === "en-US"),
    "Installed en-US is shown while remote is pending"
  );
  ok(
    !children.some(el => el.localName === "hr"),
    "No separator present while remote is pending"
  );

  // Now resolve the remote fetch and verify remote locales appear.
  resolveRemote(["de"].map(createRemoteLangpack));
  await waitForRemoteSeparator(win);

  children = Array.from(sc.controlEl.children);
  let hrIndex = children.findIndex(el => el.localName === "hr");
  let remoteValues = children.slice(hrIndex + 1).map(el => el.value);
  Assert.deepEqual(remoteValues, ["de"], "Remote locales appear after resolve");

  sandbox.restore();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// Remote locales should only be fetched once the user navigates to the
// languages pane, not when prefs are opened to a different pane.
add_task(async function testRemoteLocalesNotFetchedUntilLanguagesPaneShown() {
  let sandbox = sinon.createSandbox();
  sandbox
    .stub(LangPackMatcher.mockable, "getAvailableLangpacks")
    .resolves(["de"].map(createRemoteLangpack));

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.settings-redesign.enabled", true],
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
    ],
  });

  let paneInitialized = TestUtils.topicObserved("languages-pane-loaded");
  await openPreferencesViaOpenPreferencesAPI("general", { leaveOpen: true });
  let doc = gBrowser.contentDocument;
  let win = doc.defaultView;

  // findInPage.js initializes all panes in a requestIdleCallback after the
  // initial pane loads. Wait for the browserLanguagePreferred select to be
  // populated so that the languages.mjs settings have run their refresh and
  // remote language fetching has had a chance to run. If it always loads we
  // should have hit the API at that point.
  await paneInitialized;
  let preferredLanguage = getSettingControl("browserLanguagePreferred", win);
  await BrowserTestUtils.waitForMutationCondition(
    preferredLanguage,
    { childList: true, subtree: true },
    () => preferredLanguage.controlEl?.children?.length
  );

  is(
    LangPackMatcher.mockable.getAvailableLangpacks.callCount,
    0,
    "Remote langpacks aren't fetched while a different pane is shown"
  );

  let paneLoaded = waitForPaneChange("languages");
  synthesizeClick(doc.getElementById("category-languages"));
  await paneLoaded;
  await waitForRemoteSeparator(win);

  is(
    LangPackMatcher.mockable.getAvailableLangpacks.callCount,
    1,
    "Remote langpacks are fetched after switching to the languages pane"
  );

  let sc = getSettingControl("browserLanguagePreferred", win);
  let children = Array.from(sc.controlEl.children);
  let hrIndex = children.findIndex(el => el.localName === "hr");
  let remoteValues = children.slice(hrIndex + 1).map(el => el.value);
  Assert.deepEqual(
    remoteValues,
    ["de"],
    "Remote locales appear after switching to the languages pane"
  );

  sandbox.restore();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// When downloadEnabled is true, remote locales from AMO should appear after
// a separator below the installed locales.
add_task(async function testRemoteLocalesAppearAfterSeparator() {
  let sandbox = sinon.createSandbox();
  sandbox
    .stub(LangPackMatcher.mockable, "getAvailableLangpacks")
    .resolves(["de", "fr"].map(createRemoteLangpack));

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.settings-redesign.enabled", true],
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
    ],
  });

  let doc = await openPrefs();
  let win = doc.defaultView;
  await waitForRemoteSeparator(win);

  let sc = getSettingControl("browserLanguagePreferred", win);
  let children = Array.from(sc.controlEl.children);
  let hrIndex = children.findIndex(el => el.localName === "hr");

  Assert.greater(hrIndex, 0, "Separator appears after installed locales");

  let installedValues = children.slice(0, hrIndex).map(el => el.value);
  let remoteValues = children.slice(hrIndex + 1).map(el => el.value);

  ok(installedValues.includes("en-US"), "en-US is in the installed section");
  Assert.deepEqual(
    remoteValues,
    ["de", "fr"],
    "Remote locales appear after separator"
  );

  is(
    LangPackMatcher.mockable.getAvailableLangpacks.callCount,
    1,
    "getAvailableLangpacks was called once to fetch remote locales"
  );

  // Trigger a setting refresh by toggling a pref that the remote locales
  // setting listens to, then verify the list was cached and not re-fetched.
  Services.prefs.setBoolPref("intl.multilingual.downloadEnabled", false);
  Services.prefs.setBoolPref("intl.multilingual.downloadEnabled", true);

  // Wait for the refresh to propagate to the preferred dropdown.
  await waitForSettingControlChange(sc);

  is(
    LangPackMatcher.mockable.getAvailableLangpacks.callCount,
    1,
    "getAvailableLangpacks was not called again after refresh (cached)"
  );

  sandbox.restore();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// Locales that are already installed should not also appear in the remote section.
add_task(async function testInstalledLocalesNotDuplicatedInRemoteSection() {
  let sandbox = sinon.createSandbox();
  sandbox
    .stub(LangPackMatcher.mockable, "getAvailableLangpacks")
    .resolves(["de", "fr"].map(createRemoteLangpack));

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.settings-redesign.enabled", true],
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
      ["extensions.langpacks.signatures.required", false],
    ],
  });

  let addon = await installLangpack("fr");
  let doc = await openPrefs();
  let win = doc.defaultView;
  await waitForRemoteSeparator(win);

  let sc = getSettingControl("browserLanguagePreferred", win);
  let children = Array.from(sc.controlEl.children);
  let hrIndex = children.findIndex(el => el.localName === "hr");

  let installedValues = children.slice(0, hrIndex).map(el => el.value);
  let remoteValues = children.slice(hrIndex + 1).map(el => el.value);

  ok(
    installedValues.includes("fr"),
    "Installed fr appears in the installed section"
  );
  ok(
    !remoteValues.includes("fr"),
    "Installed fr is not duplicated in the remote section"
  );
  Assert.deepEqual(
    remoteValues,
    ["de"],
    "Only non-installed de appears in the remote section"
  );

  await addon.uninstall();
  sandbox.restore();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// Selecting a remote locale should trigger langpack installation and, when
// liveReload is off, show the restart confirmation.
add_task(
  async function testSelectingRemoteLocaleInstallsLangpackAndShowsRestart() {
    let sandbox = sinon.createSandbox();
    sandbox
      .stub(LangPackMatcher.mockable, "getAvailableLangpacks")
      .resolves(["fr"].map(createRemoteLangpack));
    sandbox.stub(LangPackMatcher.mockable, "installLangPack").resolves(true);

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.settings-redesign.enabled", true],
        ["intl.multilingual.enabled", true],
        ["intl.multilingual.downloadEnabled", true],
        ["intl.multilingual.liveReload", false],
        ["intl.multilingual.liveReloadBidirectional", false],
        ["intl.locale.requested", "en-US"],
      ],
    });

    let doc = await openPrefs();
    let win = doc.defaultView;
    await waitForRemoteSeparator(win);
    assertRestartMessageHidden(doc, true);

    let sc = getSettingControl("browserLanguagePreferred", win);
    await changeMozSelectValue(sc.controlEl, "fr");

    ok(
      LangPackMatcher.mockable.installLangPack.calledOnce,
      "installLangPack was called for the remote locale"
    );
    is(
      LangPackMatcher.mockable.installLangPack.firstCall.args[0].target_locale,
      "fr",
      "installLangPack was called with the fr langpack"
    );

    await waitForRestartMessage(doc, true);

    sandbox.restore();
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
);

// If the langpack installation throws, set() should reset the dropdown to the
// current locale rather than leaving it on the failed remote locale.
add_task(async function testFailedRemoteLocaleInstallResetsDropdown() {
  let sandbox = sinon.createSandbox();
  sandbox
    .stub(LangPackMatcher.mockable, "getAvailableLangpacks")
    .resolves(["fr"].map(createRemoteLangpack));
  sandbox
    .stub(LangPackMatcher.mockable, "installLangPack")
    .rejects(new Error("Simulated install failure"));

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.settings-redesign.enabled", true],
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
      ["intl.locale.requested", "en-US"],
    ],
  });

  let doc = await openPrefs();
  let win = doc.defaultView;
  await waitForRemoteSeparator(win);

  let sc = getSettingControl("browserLanguagePreferred", win);
  await changeMozSelectValue(sc.controlEl, "fr");

  is(
    sc.controlEl.value,
    "en-US",
    "Dropdown resets to current locale after failed install"
  );

  // An error message should be shown after a failed install.
  let messageControl = getSettingControl("browserLanguageMessage", win);
  await BrowserTestUtils.waitForMutationCondition(
    messageControl,
    { attributes: true, attributeFilter: ["hidden"] },
    () => !messageControl.hidden
  );
  ok(
    messageControl.controlEl.shadowRoot.querySelector(
      "moz-message-bar[type=error]"
    ),
    "Error message bar is shown after failed install"
  );

  sandbox.restore();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// Both language selects should be disabled while a locale is being downloaded
// and re-enabled when the download completes.
add_task(async function testSelectsDisabledDuringDownload() {
  let sandbox = sinon.createSandbox();
  sandbox
    .stub(LangPackMatcher.mockable, "getAvailableLangpacks")
    .resolves(["fr"].map(createRemoteLangpack));
  let resolveInstall;
  sandbox.stub(LangPackMatcher.mockable, "installLangPack").callsFake(
    () =>
      new Promise(resolve => {
        resolveInstall = resolve;
      })
  );

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.settings-redesign.enabled", true],
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
      ["intl.multilingual.liveReload", false],
      ["intl.multilingual.liveReloadBidirectional", false],
      ["intl.locale.requested", "en-US"],
      ["extensions.langpacks.signatures.required", false],
    ],
  });

  let addon = await installLangpack("de");
  let doc = await openPrefs();
  let win = doc.defaultView;
  await waitForRemoteSeparator(win);

  let preferred = getSettingControl("browserLanguagePreferred", win);
  let fallback = getSettingControl("browserLanguageFallback", win);
  await changeMozSelectValue(preferred.controlEl, "de");
  await waitForSettingVisible("browserLanguageFallback", win);

  ok(!preferred.controlEl.disabled, "Preferred is enabled before download");
  ok(!fallback.controlEl.disabled, "Fallback is enabled before download");

  // Trigger a remote install (don't await, it will pend).
  let setPromise = changeMozSelectValue(preferred.controlEl, "fr");

  // Wait for the installing state to propagate to the UI.
  await waitForSettingControlChange(preferred);
  ok(preferred.controlEl.disabled, "Preferred is disabled during download");
  ok(fallback.controlEl.disabled, "Fallback is disabled during download");

  resolveInstall(true);
  await setPromise;

  // Wait for re-enable after download completes.
  await waitForSettingControlChange(preferred);
  ok(!preferred.controlEl.disabled, "Preferred is re-enabled after download");
  ok(!fallback.controlEl.disabled, "Fallback is re-enabled after download");

  await addon.uninstall();
  sandbox.restore();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// --- Fallback language tests (SRD only) ---

// The fallback dropdown should be hidden when only one language is installed.
add_task(async function testFallbackHiddenWithSingleLanguage() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.settings-redesign.enabled", true],
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", false],
    ],
  });

  is(Services.locale.availableLocales.length, 1, "Only one language available");

  let doc = await openPrefs();
  let win = doc.defaultView;
  await waitForLanguageUI(doc, true);

  let fallbackControl = getSettingControl("browserLanguageFallback", win);
  is(
    fallbackControl.hidden,
    true,
    "Fallback dropdown is hidden with one language"
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// The fallback dropdown should appear when >=2 languages are installed and the
// preferred language isn't the default locale.
add_task(async function testFallbackVisibleWithMultipleLanguages() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.settings-redesign.enabled", true],
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", false],
      ["extensions.langpacks.signatures.required", false],
    ],
  });

  let addon = await installLangpack("fr");
  let doc = await openPrefs();
  let win = doc.defaultView;
  await waitForLanguageUI(doc, true);

  // Fallback stays hidden while the preferred language is still the default.
  let fallbackControl = getSettingControl("browserLanguageFallback", win);
  is(
    fallbackControl.hidden,
    true,
    "Fallback is hidden while preferred matches the default locale"
  );

  await changeLocale(doc, "fr", true);
  await waitForSettingVisible("browserLanguageFallback", win);

  await addon.uninstall();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// The fallback dropdown should remain hidden when the preferred language is
// the default locale, even with multiple languages installed.
add_task(async function testFallbackHiddenWhenPreferredIsDefault() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.settings-redesign.enabled", true],
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", false],
      ["extensions.langpacks.signatures.required", false],
    ],
  });

  let addons = await Promise.all(["fr", "de"].map(installLangpack));
  let doc = await openPrefs();
  let win = doc.defaultView;
  await waitForLanguageUI(doc, true);

  let fallbackControl = getSettingControl("browserLanguageFallback", win);
  is(
    fallbackControl.hidden,
    true,
    "Fallback is hidden when preferred equals the default locale"
  );

  await Promise.all(addons.map(addon => addon.uninstall()));
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// The fallback dropdown should only contain installed locales, not remote ones.
add_task(async function testFallbackOnlyShowsInstalledLocales() {
  let sandbox = sinon.createSandbox();
  sandbox
    .stub(LangPackMatcher.mockable, "getAvailableLangpacks")
    .resolves(["de", "it"].map(createRemoteLangpack));

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.settings-redesign.enabled", true],
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
      ["extensions.langpacks.signatures.required", false],
    ],
  });

  let addon = await installLangpack("fr");
  let doc = await openPrefs();
  let win = doc.defaultView;
  await waitForLanguageUI(doc, true);

  // Fallback only becomes visible once preferred differs from the default.
  await changeLocale(doc, "fr", true);

  let fallbackControl = getSettingControl("browserLanguageFallback", win);
  await waitForSettingVisible("browserLanguageFallback", win);

  let children = Array.from(fallbackControl.controlEl.children);
  let visibleOptions = children.filter(el => !el.hidden).map(el => el.value);
  Assert.deepEqual(
    visibleOptions,
    ["en-US"],
    "Fallback only shows installed locales, excluding preferred"
  );
  ok(!visibleOptions.includes("de"), "Remote-only locale de not in fallback");
  ok(!visibleOptions.includes("it"), "Remote-only locale it not in fallback");

  await addon.uninstall();
  sandbox.restore();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// The fallback dropdown should not include the currently preferred language.
add_task(async function testFallbackExcludesPreferredLanguage() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.settings-redesign.enabled", true],
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", false],
      ["intl.locale.requested", "en-US"],
      ["extensions.langpacks.signatures.required", false],
    ],
  });

  let addons = await Promise.all(["fr", "de"].map(installLangpack));
  let doc = await openPrefs();
  let win = doc.defaultView;
  await waitForLanguageUI(doc, true);

  // Make preferred non-default so the fallback dropdown appears.
  await changeLocale(doc, "fr", true);

  let fallbackControl = getSettingControl("browserLanguageFallback", win);
  await waitForSettingVisible("browserLanguageFallback", win);

  let children = Array.from(fallbackControl.controlEl.children);
  let fr = children.find(el => el.value === "fr");
  ok(fr?.hidden, "Preferred locale fr is hidden in fallback options");
  let visibleOptions = children.filter(el => !el.hidden).map(el => el.value);
  ok(
    visibleOptions.includes("en-US"),
    "Installed en-US is in fallback options"
  );
  ok(visibleOptions.includes("de"), "Installed de is in fallback options");

  await Promise.all(addons.map(addon => addon.uninstall()));
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// Changing the fallback language when liveReload is off should show a restart
// message and record the "reorder" telemetry event.
add_task(async function testFallbackChangeShowsRestart() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.settings-redesign.enabled", true],
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", false],
      ["intl.multilingual.liveReload", true],
      ["intl.multilingual.liveReloadBidirectional", true],
      ["intl.locale.requested", "en-US"],
      ["extensions.langpacks.signatures.required", false],
    ],
  });

  let addons = await Promise.all(["fr", "de"].map(installLangpack));
  let doc = await openPrefs();
  let win = doc.defaultView;
  await waitForLanguageUI(doc, true);

  // Live-reload to fr so preferred != default (fallback becomes visible)
  // without triggering a restart message.
  await changeLocale(doc, "fr", true);
  await BrowserTestUtils.waitForCondition(
    () => Services.locale.requestedLocales[0] === "fr",
    "fr is live-applied"
  );

  let fallbackControl = getSettingControl("browserLanguageFallback", win);
  await waitForSettingVisible("browserLanguageFallback", win);
  assertRestartMessageHidden(doc, true);

  // Turn off live reload so a fallback change triggers the restart flow.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["intl.multilingual.liveReload", false],
      ["intl.multilingual.liveReloadBidirectional", false],
    ],
  });

  await changeMozSelectValue(fallbackControl.controlEl, "de");
  await waitForRestartMessage(doc, true);

  await Promise.all(addons.map(addon => addon.uninstall()));
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
