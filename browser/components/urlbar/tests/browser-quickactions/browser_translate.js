/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests for the translate quick action.
 */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/toolkit/components/translations/tests/browser/shared-head.js",
  this
);

/**
 * Whether the named QuickAction is visible in the results.
 *
 * @param {string} name
 * @returns {boolean}
 */
const isActionVisible = name => {
  const action = window.document.querySelector(
    `.urlbarView-action-btn[data-action=${name}]`
  );

  return action !== null && BrowserTestUtils.isVisible(action);
};

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.quickactions.enabled", true],
      ["browser.translations.quickAction.enabled", true],
      ["browser.translations.enable", true],
      ["browser.urlbar.quickactions.timesShownOnboardingLabel", 0],
    ],
  });

  const { removeMocks } = await createAndMockRemoteSettings({
    languagePairs: [
      { fromLang: "en", toLang: "fr" },
      { fromLang: "fr", toLang: "en" },
    ],
  });

  registerCleanupFunction(async () => {
    await SpecialPowers.popPrefEnv();
    await removeMocks();
  });
});

add_task(async function test_translate_disabled() {
  info("Disable the translate quick action and ensure it is hidden");
  await SpecialPowers.pushPrefEnv({
    set: [["browser.translations.quickAction.enabled", false]],
  });

  info("Search for the translate quick action keyword");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "translate",
  });

  Assert.ok(
    !isActionVisible("translate"),
    "Translate action is not shown when disabled"
  );

  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_translate_ai_feature_toggle_from_disabled() {
  info("Start from disabled and ensure the translate quick action is hidden");
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ai.control.default", "blocked"],
      ["browser.ai.control.translations", "blocked"],
      ["browser.translations.enable", false],
    ],
  });

  await TranslationsParent.AIFeature.block();

  info("Search for the translate quick action keyword");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "translate",
  });

  Assert.ok(
    !isActionVisible("translate"),
    "Translate action is not shown when the AI feature is disabled"
  );

  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });

  await TranslationsParent.AIFeature.enable();

  info("Search for the translate quick action keyword after enable");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "translate",
  });

  Assert.ok(isActionVisible("translate"), 'We found action "translate"');

  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });

  await TranslationsParent.AIFeature.block();

  info("Search for the translate quick action keyword after disable again");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "translate",
  });

  Assert.ok(
    !isActionVisible("translate"),
    "Translate action is not shown when the AI feature is disabled"
  );

  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_translate_ai_feature_toggle_from_enabled() {
  info("Start from enabled and ensure the translate quick action is visible");
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ai.control.default", "available"],
      ["browser.ai.control.translations", "default"],
      ["browser.translations.enable", true],
    ],
  });

  await TranslationsParent.AIFeature.enable();

  info("Search for the translate quick action keyword");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "translate",
  });

  Assert.ok(isActionVisible("translate"), 'We found action "translate"');

  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });

  await TranslationsParent.AIFeature.block();

  info("Search for the translate quick action keyword after disable");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "translate",
  });

  Assert.ok(
    !isActionVisible("translate"),
    "Translate action is not shown when the AI feature is disabled"
  );

  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });

  await TranslationsParent.AIFeature.enable();

  info("Search for the translate quick action keyword after enable");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "translate",
  });

  Assert.ok(isActionVisible("translate"), 'We found action "translate"');

  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_translate_keyword() {
  info("Search with the primary translate keyword");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "translate",
  });

  Assert.ok(isActionVisible("translate"), 'We found action "translate"');

  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });
});

add_task(async function test_partial_match() {
  info("Search with a partial translate prefix");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "transl",
  });

  Assert.ok(isActionVisible("translate"), 'We found action "translate"');

  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });
});

add_task(async function test_translate_opens_about_translations() {
  info("Open a new tab for the translate action result");
  const translateTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  info("Search for the translate quick action");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "translate",
  });

  Assert.ok(isActionVisible("translate"), 'We found action "translate"');

  info("Wait for about:translations to load and verify");
  const translationsLoaded = BrowserTestUtils.browserLoaded(
    translateTab.linkedBrowser,
    false,
    url => url.startsWith("about:translations")
  );
  EventUtils.synthesizeKey("KEY_Tab", {}, window);
  EventUtils.synthesizeKey("KEY_Enter", {}, window);

  await translationsLoaded;

  Assert.ok(
    translateTab.linkedBrowser.currentURI.spec.startsWith("about:translations"),
    "about:translations page is loaded"
  );

  if (UrlbarTestUtils.isPopupOpen(window)) {
    await UrlbarTestUtils.promisePopupClose(window, () => {
      EventUtils.synthesizeKey("KEY_Escape");
    });
  }
  BrowserTestUtils.removeTab(translateTab);
});

add_task(async function test_translate_includes_target_language() {
  info("Open a new tab for the translate action result");
  const translateTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  info("Search for the translate quick action");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "translate",
  });

  Assert.ok(isActionVisible("translate"), 'We found action "translate"');

  info("Wait for about:translations to load and check hash parameters");
  const translationsLoaded = BrowserTestUtils.browserLoaded(
    translateTab.linkedBrowser,
    false,
    url => url.startsWith("about:translations")
  );
  EventUtils.synthesizeKey("KEY_Tab", {}, window);
  EventUtils.synthesizeKey("KEY_Enter", {}, window);

  await translationsLoaded;

  const url = new URL(translateTab.linkedBrowser.currentURI.spec);
  const hashParameters = new URLSearchParams(url.hash.substring(1));
  const targetLanguage = hashParameters.get("trg");

  Assert.equal(
    targetLanguage,
    "en",
    "Target language parameter is set to 'en'"
  );

  if (UrlbarTestUtils.isPopupOpen(window)) {
    await UrlbarTestUtils.promisePopupClose(window, () => {
      EventUtils.synthesizeKey("KEY_Escape");
    });
  }
  BrowserTestUtils.removeTab(translateTab);
});

add_task(
  async function test_translate_missing_language_does_not_append_target() {
    info("Open a new tab for the translate action result");
    const translateTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "about:blank"
    );

    info("Temporarily override preferred language lookup to fail once");
    let oneTimeOverrideCalled = false;
    const originalGetTopPreferredSupportedToLang =
      TranslationsParent.getTopPreferredSupportedToLang;
    TranslationsParent.getTopPreferredSupportedToLang = async () => {
      oneTimeOverrideCalled = true;
      TranslationsParent.getTopPreferredSupportedToLang =
        originalGetTopPreferredSupportedToLang;
      throw new Error("Simulated failure retrieving the preferred language");
    };

    info("Search for the translate quick action");
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "translate",
    });

    Assert.ok(isActionVisible("translate"), 'We found action "translate"');

    info(
      "Wait for about:translations and confirm no target language is appended"
    );
    const translationsLoaded = BrowserTestUtils.browserLoaded(
      translateTab.linkedBrowser,
      false,
      url => url.startsWith("about:translations")
    );
    EventUtils.synthesizeKey("KEY_Tab", {}, window);
    EventUtils.synthesizeKey("KEY_Enter", {}, window);
    await translationsLoaded;

    const url = new URL(translateTab.linkedBrowser.currentURI.spec);
    const hashParameters = new URLSearchParams(url.hash.substring(1));

    Assert.equal(
      hashParameters.get("src"),
      "detect",
      'The default source language parameter is "detect"'
    );
    Assert.equal(
      hashParameters.get("trg"),
      null,
      "No target language parameter is appended"
    );
    Assert.ok(oneTimeOverrideCalled, "The overridden language lookup ran once");
    Assert.equal(
      originalGetTopPreferredSupportedToLang,
      TranslationsParent.getTopPreferredSupportedToLang,
      "The preferred-language getter has been restored"
    );

    if (UrlbarTestUtils.isPopupOpen(window)) {
      await UrlbarTestUtils.promisePopupClose(window, () => {
        EventUtils.synthesizeKey("KEY_Escape");
      });
    }
    BrowserTestUtils.removeTab(translateTab);
  }
);

add_task(async function test_translate_switches_to_existing_tab() {
  info("Open about:translations in the first tab");
  const translateTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:translations"
  );

  info("Open a content page in another foreground tab");
  const otherTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );

  Assert.equal(
    gBrowser.selectedTab,
    otherTab,
    "Other tab is currently selected"
  );

  info("Trigger the translate quick action");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "translate",
  });

  Assert.ok(isActionVisible("translate"), 'We found action "translate"');

  const tabSelected = BrowserTestUtils.waitForEvent(translateTab, "TabSelect");
  EventUtils.synthesizeKey("KEY_Tab", {}, window);
  EventUtils.synthesizeKey("KEY_Enter", {}, window);

  await tabSelected;

  Assert.equal(
    gBrowser.selectedTab,
    translateTab,
    "Switched to existing about:translations tab"
  );

  if (UrlbarTestUtils.isPopupOpen(window)) {
    await UrlbarTestUtils.promisePopupClose(window, () => {
      EventUtils.synthesizeKey("KEY_Escape");
    });
  }
  BrowserTestUtils.removeTab(otherTab);
  BrowserTestUtils.removeTab(translateTab);
});

/**
 * This test case ensures that the translate Quick Action opens and reuses a
 * standalone page while preserving an empty split pane and its companion.
 */
add_task(async function test_translate_ignores_split_view_tabs() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.splitView.enabled", true]],
  });

  const tabs = [];
  let splitView = null;

  try {
    const splitTranslationsTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "about:translations"
    );
    tabs.push(splitTranslationsTab);

    const blankTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "about:blank"
    );
    tabs.push(blankTab);
    await SplitViewTranslationsTestUtils.openExistingTabsInSplitView(
      blankTab,
      splitTranslationsTab
    );
    splitView = blankTab.splitview;
    Assert.equal(
      gBrowser.selectedTab,
      blankTab,
      "The blank tab remains selected after opening Split View"
    );
    Assert.ok(blankTab.isEmpty, "The selected split pane is empty");

    info(
      "Invoke the translate Quick Action with the empty split pane selected"
    );
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "translate",
    });
    Assert.ok(isActionVisible("translate"), 'We found action "translate"');
    const translationsOpened = BrowserTestUtils.waitForNewTab(
      gBrowser,
      url => url.startsWith("about:translations"),
      true
    );
    EventUtils.synthesizeMouseAtCenter(
      window.document.querySelector(
        ".urlbarView-action-btn[data-action=translate]"
      ),
      {},
      window
    );
    const fullPageTranslationsTab = await translationsOpened;
    tabs.push(fullPageTranslationsTab);

    Assert.equal(
      gBrowser.selectedTab,
      fullPageTranslationsTab,
      "The QuickAction opened about:translations in the standalone tab"
    );
    Assert.ok(
      !fullPageTranslationsTab.splitview,
      "The new translations page is not in Split View"
    );
    Assert.equal(
      splitTranslationsTab.splitview,
      splitView,
      "Opening the standalone page preserves the existing companion"
    );
    Assert.equal(
      blankTab.splitview,
      splitView,
      "The empty pane stays paired with the companion"
    );
    Assert.equal(
      blankTab.linkedBrowser.currentURI.spec,
      "about:blank",
      "The Quick Action leaves the empty pane blank"
    );

    info("The QuickAction reuses the standalone page instead of the companion");
    const otherTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "https://example.org"
    );
    tabs.push(otherTab);

    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "translate",
    });
    Assert.ok(isActionVisible("translate"), 'We found action "translate"');
    const tabSelected = BrowserTestUtils.waitForEvent(
      fullPageTranslationsTab,
      "TabSelect"
    );
    const tabCount = gBrowser.tabs.length;
    EventUtils.synthesizeMouseAtCenter(
      window.document.querySelector(
        ".urlbarView-action-btn[data-action=translate]"
      ),
      {},
      window
    );

    await tabSelected;
    Assert.equal(
      gBrowser.selectedTab,
      fullPageTranslationsTab,
      "The QuickAction selected the full-page about:translations tab"
    );
    Assert.equal(
      splitTranslationsTab.splitview,
      splitView,
      "The about:translations tab in Split View was ignored"
    );
    Assert.equal(
      gBrowser.tabs.length,
      tabCount,
      "Reusing the standalone translations page does not create another tab"
    );
  } finally {
    if (UrlbarTestUtils.isPopupOpen(window)) {
      await UrlbarTestUtils.promisePopupClose(window, () => {
        EventUtils.synthesizeKey("KEY_Escape");
      });
    }
    splitView?.unsplitTabs();
    for (const tab of tabs) {
      if (tab.isConnected) {
        await BrowserTestUtils.removeTab(tab);
      }
    }
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_translate_reenabled() {
  info("Disable the translate quick action and ensure it is hidden");
  await SpecialPowers.pushPrefEnv({
    set: [["browser.translations.quickAction.enabled", false]],
  });

  info("Search for the translate quick action keyword");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "tran",
  });

  Assert.ok(
    !isActionVisible("translate"),
    "Translate action is not shown when disabled"
  );

  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });

  await SpecialPowers.popPrefEnv();

  info("Search for the translate quick action keyword");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "tran",
  });

  Assert.ok(isActionVisible("translate"), "Translate action is now shown");

  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });
});
