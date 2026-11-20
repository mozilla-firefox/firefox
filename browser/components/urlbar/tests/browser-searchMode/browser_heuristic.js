/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests heuristic results in search mode.
 */

"use strict";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.unifiedSearchButton.always", false],
      ["browser.urlbar.unifiedSearchButton.historyInSearchMode", false],
    ],
  });
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.scotchBonnet.enableOverride", false]],
  });
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();

  // Add a new mock default engine so we don't hit the network.
  await SearchTestUtils.installSearchExtension(
    { name: "Test" },
    { setAsDefault: true }
  );

  // Add one bookmark we'll use below.
  await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: "https://example.com/bookmark",
  });
  registerCleanupFunction(async () => {
    await PlacesUtils.bookmarks.eraseEverything();
  });
});

// Enters search mode with no results.
add_task(async function noResults() {
  // Do a search that doesn't match our bookmark and enter bookmark search mode.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "doesn't match anything",
  });
  await UrlbarTestUtils.enterSearchMode(window, {
    source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
  });

  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    0,
    "Zero results since no bookmark matches"
  );

  // Press enter.  Nothing should happen.
  let promise = waitForLoadStartOrTimeout();
  EventUtils.synthesizeKey("KEY_Enter");
  await Assert.rejects(promise, /timed out/, "Nothing should have loaded");

  await UrlbarTestUtils.promisePopupClose(window);
});

// Enters a local search mode (bookmarks) with a matching result.  No heuristic
// should be present.
add_task(async function localNoHeuristic() {
  // Do a search that matches our bookmark and enter bookmarks search mode.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "bookmark",
  });
  await UrlbarTestUtils.enterSearchMode(window, {
    source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
  });

  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    1,
    "There should be one result"
  );

  let result = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(
    result.source,
    UrlbarShared.RESULT_SOURCE.BOOKMARKS,
    "Result source should be BOOKMARKS"
  );
  Assert.equal(
    result.type,
    UrlbarShared.RESULT_TYPE.URL,
    "Result type should be URL"
  );
  Assert.equal(
    result.url,
    "https://example.com/bookmark",
    "Result URL is our bookmark URL"
  );
  Assert.ok(!result.heuristic, "Result should not be heuristic");

  // Press enter.  Nothing should happen.
  let promise = waitForLoadStartOrTimeout();
  EventUtils.synthesizeKey("KEY_Enter");
  await Assert.rejects(promise, /timed out/, "Nothing should have loaded");

  await UrlbarTestUtils.promisePopupClose(window);
});

// Enters a local search mode (bookmarks) with a matching autofill result.  The
// result should be the heuristic.
add_task(async function localAutofill() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    // Do a search that autofills our bookmark's origin and enter bookmarks
    // search mode.
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "example",
    });
    await UrlbarTestUtils.enterSearchMode(window, {
      source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
    });

    Assert.equal(
      UrlbarTestUtils.getResultCount(window),
      2,
      "There should be two results"
    );

    let result = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
    Assert.equal(
      result.source,
      UrlbarShared.RESULT_SOURCE.HISTORY,
      "Result source should be HISTORY"
    );
    Assert.equal(
      result.type,
      UrlbarShared.RESULT_TYPE.URL,
      "Result type should be URL"
    );
    Assert.equal(
      result.url,
      "https://example.com/",
      "Result URL is our bookmark's origin"
    );
    Assert.ok(result.heuristic, "Result should be heuristic");
    Assert.ok(result.autofill, "Result should be autofill");

    result = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
    Assert.equal(
      result.source,
      UrlbarShared.RESULT_SOURCE.BOOKMARKS,
      "Result source should be BOOKMARKS"
    );
    Assert.equal(
      result.type,
      UrlbarShared.RESULT_TYPE.URL,
      "Result type should be URL"
    );
    Assert.equal(
      result.url,
      "https://example.com/bookmark",
      "Result URL is our bookmark URL"
    );

    // Press enter.  Our bookmark's origin should be loaded.
    let loadPromise = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
    EventUtils.synthesizeKey("KEY_Enter");
    await loadPromise;
    Assert.equal(
      gBrowser.currentURI.spec,
      "https://example.com/",
      "Bookmark's origin should have loaded"
    );
  });
});

// Enters a remote engine search mode.  There should be a heuristic.
add_task(async function remote() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    // Do a search and enter search mode with our test engine.
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "remote",
    });
    await UrlbarTestUtils.enterSearchMode(window, {
      engineName: "Test",
    });

    Assert.equal(
      UrlbarTestUtils.getResultCount(window),
      1,
      "There should be one result"
    );

    let result = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
    Assert.equal(
      result.source,
      UrlbarShared.RESULT_SOURCE.SEARCH,
      "Result source should be SEARCH"
    );
    Assert.equal(
      result.type,
      UrlbarShared.RESULT_TYPE.SEARCH,
      "Result type should be SEARCH"
    );
    Assert.ok(result.searchParams, "searchParams should be present");
    Assert.equal(
      result.searchParams.engine,
      "Test",
      "searchParams.engine should be our test engine"
    );
    Assert.equal(
      result.searchParams.query,
      "remote",
      "searchParams.query should be our query"
    );
    Assert.ok(result.heuristic, "Result should be heuristic");

    // Press enter.  The engine's SERP should load.
    let loadPromise = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
    EventUtils.synthesizeKey("KEY_Enter");
    await loadPromise;
    Assert.equal(
      gBrowser.currentURI.spec,
      "https://example.com/?q=remote",
      "Engine's SERP should have loaded"
    );
  });
});

// Presses Alt+Enter in a remote engine search mode.  The engine's SERP should
// open in a new tab, and the original tab should be reverted.
add_task(async function remoteAltEnter() {
  await BrowserTestUtils.withNewTab("about:robots", async () => {
    let originalTab = gBrowser.selectedTab;
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "remote",
      fireInputEvent: true,
    });
    await UrlbarTestUtils.enterSearchMode(window, {
      engineName: "Test",
    });

    let tabOpenPromise = BrowserTestUtils.waitForNewTab(
      gBrowser,
      "https://example.com/?q=remote",
      true
    );
    EventUtils.synthesizeKey("KEY_Enter", { altKey: true });
    let newTab = await tabOpenPromise;
    Assert.equal(gBrowser.selectedTab, newTab, "The SERP opened in a new tab");

    await BrowserTestUtils.switchTab(gBrowser, originalTab);
    await UrlbarTestUtils.assertSearchMode(window, null);
    Assert.equal(gURLBar.value, "about:robots", "The URL should be restored");
    Assert.ok(!gURLBar.view.isOpen, "The view should be closed");
    Assert.ok(!gURLBar.focused, "The input should not be focused");

    BrowserTestUtils.removeTab(newTab);
  });
});

// Types a URL in a remote engine search mode.  The heuristic should search for
// it.
add_task(async function remoteUrl() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "example.org",
    });
    await UrlbarTestUtils.enterSearchMode(window, {
      engineName: "Test",
    });

    let result = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
    Assert.equal(
      result.type,
      UrlbarShared.RESULT_TYPE.SEARCH,
      "Result type should be SEARCH"
    );
    Assert.ok(result.heuristic, "Result should be heuristic");

    let loadPromise = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
    EventUtils.synthesizeKey("KEY_Enter");
    await loadPromise;
    Assert.equal(
      gBrowser.currentURI.spec,
      "https://example.com/?q=example.org",
      "Engine's SERP should have loaded"
    );
  });
});

// Types a URL in a remote engine search mode with navigation enabled in search
// mode.  The heuristic should visit it.
add_task(async function remoteUrlNavigationEnabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.unifiedSearchButton.always", true]],
  });

  // eslint-disable-next-line sdl/no-insecure-url
  const expectedUrl = "http://example.org/";
  for (let closeView of [false, true]) {
    info(`Pressing Enter with the view ${closeView ? "closed" : "open"}`);
    await BrowserTestUtils.withNewTab("about:blank", async () => {
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "example.org",
      });
      await UrlbarTestUtils.enterSearchMode(window, {
        engineName: "Test",
      });

      let result = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
      Assert.equal(
        result.type,
        UrlbarShared.RESULT_TYPE.URL,
        "Result type should be URL"
      );
      Assert.equal(result.url, expectedUrl, "Result URL is the typed URL");
      Assert.ok(result.heuristic, "Result should be heuristic");

      if (closeView) {
        await UrlbarTestUtils.promisePopupClose(window);
      }

      // Schemeless HTTPS-First may upgrade the load, so check only the host.
      let loadPromise = BrowserTestUtils.browserLoaded(
        gBrowser.selectedBrowser
      );
      EventUtils.synthesizeKey("KEY_Enter");
      await loadPromise;
      Assert.equal(
        gBrowser.currentURI.host,
        "example.org",
        "The typed URL should have loaded"
      );
    });
  }

  await SpecialPowers.popPrefEnv();
});
