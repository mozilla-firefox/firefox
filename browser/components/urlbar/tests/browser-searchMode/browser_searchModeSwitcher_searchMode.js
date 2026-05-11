/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

add_setup(async function setup() {
  await SearchTestUtils.updateRemoteSettingsConfig([{ identifier: "engine" }]);

  await PlacesUtils.history.clear();
  await UrlbarTestUtils.formHistory.clear();

  // Add "testing" to form history
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "testing",
  });
  EventUtils.synthesizeKey("KEY_Enter");
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);

  registerCleanupFunction(async function () {
    await PlacesUtils.history.clear();
    await UrlbarTestUtils.formHistory.clear();
  });
});

add_task(async function test_search_after_result_nav() {
  await BrowserTestUtils.loadURIString({
    browser: gBrowser.selectedBrowser,
    uriString: "about:blank",
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });

  info("Selecting suggestion.");
  EventUtils.synthesizeKey("KEY_ArrowDown");

  let popup = await UrlbarTestUtils.openSearchModeSwitcher(window);
  Assert.ok(
    !BrowserTestUtils.isVisible(gURLBar.view.panel),
    "The UrlbarView is not visible"
  );

  info("Press on the engine button to search for the suggestion");
  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
  let browserLoaded = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  popup.querySelector(`panel-item[data-engine-id=engine]`).click();
  EventUtils.synthesizeKey("KEY_Enter");
  await Promise.all([popupHidden, browserLoaded]);

  Assert.equal(
    gBrowser.currentURI.spec,
    SearchService.defaultEngine.getSubmission("testing").uri.spec,
    "Searched for suggestion"
  );

  info("Press the close button and escape search mode");
  gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(window, null);
});

add_task(async function test_local_searchmode_after_result_nav() {
  await BrowserTestUtils.loadURIString({
    browser: gBrowser.selectedBrowser,
    uriString: "about:blank",
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });

  info("Selecting suggestion.");
  EventUtils.synthesizeKey("KEY_ArrowDown");

  let popup = await UrlbarTestUtils.openSearchModeSwitcher(window);
  Assert.ok(
    !BrowserTestUtils.isVisible(gURLBar.view.panel),
    "The UrlbarView is not visible"
  );

  info("Press on the bookmarks button to enter local search mode");
  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
  popup.querySelector(".search-button-bookmarks").click();
  await popupHidden;

  await UrlbarTestUtils.assertSearchMode(window, {
    source: 1,
    entry: "searchbutton",
  });

  Assert.equal(gURLBar.value, "testing", "Suggestion stays in urlbar input");

  info("Press the close button and escape search mode");
  gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(window, null);
});

add_task(async function test_engine_searchmode_without_usertyped() {
  await BrowserTestUtils.loadURIString({
    browser: gBrowser.selectedBrowser,
    uriString: "https://example.com/",
  });

  await makeSearchModeSwitcherVisible();
  let popup = await UrlbarTestUtils.openSearchModeSwitcher(window);
  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
  info("Press on the engine button to enter search mode");
  popup.querySelector(`panel-item[data-engine-id=engine]`).click();
  await popupHidden;

  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "engine",
    entry: "searchbutton",
    source: 3,
  });

  Assert.equal(gURLBar.value, "", "Does not search for current URI");

  info("Press the close button and escape search mode");
  gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(window, null);
});

add_task(async function test_local_searchmode_without_usertyped() {
  await BrowserTestUtils.loadURIString({
    browser: gBrowser.selectedBrowser,
    uriString: "https://example.com/",
  });

  await makeSearchModeSwitcherVisible();
  let popup = await UrlbarTestUtils.openSearchModeSwitcher(window);
  info("Press on the bookmarks button to enter local search mode");
  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
  popup.querySelector(".search-button-bookmarks").click();
  await popupHidden;

  await UrlbarTestUtils.assertSearchMode(window, {
    source: 1,
    entry: "searchbutton",
  });

  Assert.equal(gURLBar.value, "", "Does not search for current URI");

  info("Press the close button and escape search mode");
  gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(window, null);
});

async function makeSearchModeSwitcherVisible() {
  info("Focus search mode switcher button to make it visible.");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });
  await UrlbarTestUtils.promisePopupClose(window);
  EventUtils.synthesizeKey("KEY_Tab", { shiftKey: true });
}

add_task(async function test_closeButtonFocus() {
  await BrowserTestUtils.loadURIString({
    browser: gBrowser.selectedBrowser,
    uriString: "about:blank",
  });

  await UrlbarTestUtils.activateSearchModeSwitcherItem(
    window,
    "panel-item[data-engine-id=engine]"
  );

  Assert.equal(document.activeElement, gURLBar.inputField, "Input is focused");

  // We intentionally turn off this a11y check, because the following click is
  // purposefully targeting a non-interactive element.
  AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
  EventUtils.synthesizeMouseAtCenter(gBrowser.selectedBrowser, {});
  AccessibilityUtils.resetEnv();
  Assert.equal(
    document.activeElement,
    gBrowser.selectedBrowser,
    "Content was focused"
  );
  gURLBar.querySelector(".searchmode-switcher-close").click();
  Assert.equal(document.activeElement, gURLBar.inputField, "Input was focused");
  await UrlbarTestUtils.promiseSearchComplete(window);
  Assert.ok(gURLBar.view.isOpen, "Urlbar view was opened");
});
