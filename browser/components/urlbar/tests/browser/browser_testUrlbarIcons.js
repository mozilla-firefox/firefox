/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

function getSwitcherIconUrl(win) {
  let el = win.gURLBar.querySelector(".searchmode-switcher");
  let override = win
    .getComputedStyle(el)
    .getPropertyValue("--button-icon-content");
  if (override) {
    return override.match(/url\("([^"]+)"\)/)?.[1];
  }
  return el.getAttribute("iconsrc");
}

let DEFAULT_ENGINE_ICON = null;

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.unifiedSearchButton.always", true],
      // Tests get no default Top Sites, and the empty-string openings below
      // would otherwise leave the view closed with no results.
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });

  let engine = await SearchService.getDefault();
  DEFAULT_ENGINE_ICON = await engine.getIconURL();
  registerCleanupFunction(() => PlacesUtils.history.clear());
});

// When the urlbar is unfocused / empty, the icon should be the magnifying glass.
add_task(async function test_icon_is_search_glass_when_empty() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "data:text/html,"
  );

  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });

  Assert.equal(
    getSwitcherIconUrl(window),
    UrlbarUtils.ICON.SEARCH_GLASS,
    "Icon is search glass when urlbar is unfocused"
  );

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });

  await BrowserTestUtils.waitForCondition(
    () => getSwitcherIconUrl(window) == DEFAULT_ENGINE_ICON,
    "Icon should be default engine icon when focused"
  );

  await BrowserTestUtils.removeTab(tab);
});

// When the user types a query and the top result is a search result,
// the icon should update to match the engine's icon.
add_task(async function test_icon_updates_to_engine_icon_on_search_result() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "hello",
  });

  let engine = await SearchService.getDefault();
  await BrowserTestUtils.waitForCondition(() => {
    let result = window.gURLBar.view.getResultAtIndex(0);
    return (
      result?.type == UrlbarUtils.RESULT_TYPE.SEARCH &&
      result?.payload?.engine == engine.name
    );
  }, "Waiting for a default engine SEARCH result at index 0");

  await BrowserTestUtils.waitForCondition(
    () => getSwitcherIconUrl(window) == DEFAULT_ENGINE_ICON,
    "Waiting for icon to update to the default engine's icon"
  );

  Assert.equal(
    getSwitcherIconUrl(window),
    DEFAULT_ENGINE_ICON,
    "Icon should match the default engine's icon"
  );

  await UrlbarTestUtils.promisePopupClose(window);
});

// When the top result is a URL result, the icon should be the globe.
add_task(async function test_icon_updates_to_globe_on_url_result() {
  await PlacesTestUtils.addVisits("https://example.com");

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "example.com",
  });

  await BrowserTestUtils.waitForCondition(() => {
    let result = window.gURLBar.view.getResultAtIndex(0);
    return result?.type == UrlbarUtils.RESULT_TYPE.URL;
  }, "Waiting for a URL result at index 0");

  await BrowserTestUtils.waitForCondition(
    () => getSwitcherIconUrl(window) == UrlbarUtils.ICON.GLOBE,
    "Waiting for icon to update to globe for URL result"
  );

  Assert.equal(
    getSwitcherIconUrl(window),
    UrlbarUtils.ICON.GLOBE,
    "Icon should be the globe when the top result is a URL"
  );

  await UrlbarTestUtils.promisePopupClose(window);
});

// When the top result is an autofill result, the icon should be the globe.
add_task(async function test_icon_updates_to_globe_on_autofill_result() {
  await PlacesTestUtils.addVisits("https://example.com/autofill-test");

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "example.com/autofill",
  });

  await BrowserTestUtils.waitForCondition(() => {
    let result = window.gURLBar.view.getResultAtIndex(0);
    return result?.autofill;
  }, "Waiting for an autofill result at index 0");

  await BrowserTestUtils.waitForCondition(
    () => getSwitcherIconUrl(window) == UrlbarUtils.ICON.GLOBE,
    "Waiting for icon to update to globe for autofill result"
  );

  Assert.equal(
    getSwitcherIconUrl(window),
    UrlbarUtils.ICON.GLOBE,
    "Icon should be the globe when the top result is autofilled"
  );

  await UrlbarTestUtils.promisePopupClose(window);
});
