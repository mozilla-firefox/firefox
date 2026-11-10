/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.secondaryActions.featureGate", true],
      ["browser.urlbar.scotchBonnet.persistSearchMode", true],
      ["browser.urlbar.showSearchTerms.featureGate", true],
    ],
  });
});

add_task(async function test_persist_searchmode() {
  let cleanup = await installPersistTestEngines();

  const APP_PROVIDED_ENGINE_URL = "http://mochi.test:8888/";
  let onLoaded = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    APP_PROVIDED_ENGINE_URL
  );
  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    APP_PROVIDED_ENGINE_URL
  );
  await onLoaded;

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "search",
  });

  info("Focus and select the contextual search result");
  let onLoad = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  EventUtils.synthesizeKey("KEY_Tab");
  EventUtils.sendString("test");
  EventUtils.synthesizeKey("KEY_Enter");
  await onLoad;

  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "MochiSearch",
    entry: "other",
    source: 3,
  });

  info("Performing another search in the urlbar will remain in searchMode");
  onLoad = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "new search",
  });
  EventUtils.synthesizeKey("KEY_Enter");
  await onLoad;
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "MochiSearch",
    entry: "other",
    source: 3,
  });

  info("If the user navigates to another host, we exit searchMode");
  onLoad = BrowserTestUtils.browserLoaded(
    gBrowser,
    false,
    "https://example.org/"
  );
  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    "https://example.org"
  );
  await onLoad;
  await UrlbarTestUtils.assertSearchMode(window, null);

  cleanup();
  await resetApplicationProvidedEngines();
});

add_task(async function test_persist_searchmode_non_config_engine() {
  let searchExtension = await SearchTestUtils.installSearchExtension(
    {
      name: "Contextual",
      search_url: "https://example.org/browser",
    },
    { skipUnload: true }
  );

  const ENGINE_TEST_URL = "https://example.org/";
  let onLoaded = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    ENGINE_TEST_URL
  );
  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    ENGINE_TEST_URL
  );
  await onLoaded;

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "search",
  });

  info("Focus and select the contextual search result");
  let onLoad = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  EventUtils.synthesizeKey("KEY_Tab");

  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "Contextual",
    entry: "keywordoffer",
    isPreview: true,
  });

  EventUtils.sendString("test");
  EventUtils.synthesizeKey("KEY_Enter");
  await onLoad;

  info("Should exit search mode for non config engines");
  await UrlbarTestUtils.assertSearchMode(window, null);

  await searchExtension.unload();
});

add_task(async function test_escape_searchmode() {
  let cleanup = await installPersistTestEngines();

  const APP_PROVIDED_ENGINE_URL = "http://mochi.test:8888/";
  let onLoaded = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    APP_PROVIDED_ENGINE_URL
  );
  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    APP_PROVIDED_ENGINE_URL
  );
  await onLoaded;

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "search",
  });

  info("Focus and select the contextual search result");
  let onLoad = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  EventUtils.synthesizeKey("KEY_Tab");
  EventUtils.sendString("test");
  EventUtils.synthesizeKey("KEY_Enter");
  await onLoad;

  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "MochiSearch",
    entry: "other",
    source: 3,
  });

  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeKey("l", { accelKey: true });
  });
  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });
  EventUtils.synthesizeKey("KEY_Escape");

  await UrlbarTestUtils.assertSearchMode(window, null);

  cleanup();
  await resetApplicationProvidedEngines();
});
