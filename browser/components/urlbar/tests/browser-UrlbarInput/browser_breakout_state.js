/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async function setup() {
  // Tests get no default Top Sites, and the focus task needs the empty string
  // to produce results so that the view opens.
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });

  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function focus() {
  info("Open view with some results");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    value: "",
    window,
  });
  Assert.ok(gURLBar.hasAttribute("breakout"));
  Assert.ok(gURLBar.hasAttribute("breakout-extend"));

  info("Close view by ESC");
  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });
  Assert.ok(!gURLBar.view.isOpen);
  Assert.ok(gURLBar.hasAttribute("focused"));
  Assert.ok(gURLBar.hasAttribute("breakout"));
  Assert.equal(
    gURLBar.hasAttribute("breakout-extend"),
    Services.prefs.getBoolPref("browser.nova.enabled")
  );

  info("Blur the focus from the urlbar");
  EventUtils.synthesizeKey("KEY_Escape");
  Assert.ok(!gURLBar.view.isOpen);
  Assert.ok(!gURLBar.hasAttribute("focused"));
  Assert.ok(gURLBar.hasAttribute("breakout"));
  Assert.ok(!gURLBar.hasAttribute("breakout-extend"));
});

add_task(async function empty_to_some() {
  info("Open view and change the search mode");
  let manager = ProvidersManager.getInstanceForSap("urlbar");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({ value: "", window });
  await UrlbarTestUtils.activateSearchModeSwitcherItem(
    window,
    ".search-button-history"
  );
  await UrlbarTestUtils.promiseSearchComplete(window);
  Assert.equal(UrlbarTestUtils.getResultCount(window), 0);
  Assert.ok(gURLBar.view.isOpen);
  Assert.ok(gURLBar.hasAttribute("focused"));
  Assert.ok(gURLBar.hasAttribute("breakout"));
  Assert.ok(gURLBar.hasAttribute("breakout-extend"));

  info("Open view with some results");
  let someProvider = new UrlbarTestUtils.TestProvider({
    results: [
      new UrlbarResult({
        type: UrlbarUtils.RESULT_TYPE.URL,
        source: UrlbarUtils.RESULT_SOURCE.HISTORY,
        suggestedIndex: 0,
        payload: {
          url: "https://example.com/",
          title: "example",
        },
      }),
    ],
    name: "someProvider",
    priority: Infinity,
  });
  manager.registerProvider(someProvider);

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    value: "test",
    window,
  });
  Assert.ok(gURLBar.hasAttribute("breakout"));
  Assert.ok(gURLBar.hasAttribute("breakout-extend"));

  manager.unregisterProvider(someProvider);
  await UrlbarTestUtils.promisePopupClose(window);
});

add_task(async function some_to_empty() {
  info("Open view with some results");
  let manager = ProvidersManager.getInstanceForSap("urlbar");
  let someProvider = new UrlbarTestUtils.TestProvider({
    results: [
      new UrlbarResult({
        type: UrlbarUtils.RESULT_TYPE.URL,
        source: UrlbarUtils.RESULT_SOURCE.HISTORY,
        suggestedIndex: 0,
        payload: {
          url: "https://example.com/",
          title: "example",
        },
      }),
    ],
    name: "someProvider",
    priority: Infinity,
  });
  manager.registerProvider(someProvider);

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    value: "test",
    window,
  });
  Assert.ok(gURLBar.hasAttribute("breakout"));
  Assert.ok(gURLBar.hasAttribute("breakout-extend"));
  manager.unregisterProvider(someProvider);

  info("Open view with empty results");
  let emptyProvider = new UrlbarTestUtils.TestProvider({
    results: [],
    name: "emptyProvider",
    priority: Infinity,
  });
  manager.registerProvider(emptyProvider);
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    value: "updated",
    window,
  });
  Assert.equal(UrlbarTestUtils.getResultCount(window), 0);
  Assert.ok(gURLBar.view.isOpen);
  Assert.ok(gURLBar.hasAttribute("focused"));
  Assert.ok(gURLBar.hasAttribute("breakout"));
  Assert.ok(gURLBar.hasAttribute("breakout-extend"));

  manager.unregisterProvider(emptyProvider);
  await UrlbarTestUtils.promisePopupClose(window);
});

add_task(async function oneoffs() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.scotchBonnet.enableOverride", false]],
  });

  let manager = ProvidersManager.getInstanceForSap("urlbar");
  let emptyProvider = new UrlbarTestUtils.TestProvider({
    results: [],
    name: "emptyProvider",
    priority: Infinity,
  });
  manager.registerProvider(emptyProvider);

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    value: "empty",
    window,
  });
  Assert.equal(UrlbarTestUtils.getResultCount(window), 0);
  Assert.ok(gURLBar.hasAttribute("breakout"));
  Assert.ok(gURLBar.hasAttribute("focused"));
  Assert.ok(
    gURLBar.hasAttribute("breakout-extend"),
    "breakout-extend shuld be set if there is oneoff buttons"
  );

  manager.unregisterProvider(emptyProvider);
  await UrlbarTestUtils.promisePopupClose(window);
  await SpecialPowers.popPrefEnv();
});
