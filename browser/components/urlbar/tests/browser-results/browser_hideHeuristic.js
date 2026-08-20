/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Basic smoke tests for the `browser.urlbar.experimental.hideHeuristic` pref,
// which hides the heuristic result. Each task performs a search that triggers a
// specific heuristic, verifies that it's hidden or shown as appropriate, and
// verifies that it's picked when enter is pressed.
//
// If/when it becomes the default, we should update existing tests as necessary
// and remove this one.

"use strict";

// Allow more time for Mac machines so they don't time out in verify mode.
if (AppConstants.platform == "macosx") {
  requestLongerTimeout(3);
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.experimental.hideHeuristic", true],
      ["browser.urlbar.suggest.quickactions", false],
      ["dom.security.https_first_schemeless", false],
    ],
  });
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.history.clear();
});

// UrlbarShared.RESULT_GROUP.HEURISTIC_EXTENSION should be hidden.
add_task(async function extension() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await withVisits(async visitURLs => {
      // Add an extension provider that returns a heuristic.
      let url = "http://example.com/extension-test";
      let provider = new UrlbarTestUtils.TestProvider({
        name: "ExtensionTest",
        type: UrlbarShared.PROVIDER_TYPE.EXTENSION,
        results: [
          new UrlbarResult({
            type: UrlbarShared.RESULT_TYPE.URL,
            source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
            heuristic: true,
            payload: {
              url,
              title: "Test",
            },
          }),
        ],
      });
      let providersManager = ProvidersManager.getInstanceForSap("urlbar");
      providersManager.registerProvider(provider);

      // Do a search that fetches the provider's result and check it.
      let heuristic = await search({
        value: "test",
        expectedGroup: UrlbarShared.RESULT_GROUP.HEURISTIC_EXTENSION,
      });
      Assert.equal(heuristic.payload.url, url, "Heuristic URL is correct");

      // Check the other visit results.
      await checkVisitResults(visitURLs);

      // Press enter to verify the heuristic result is loaded.
      await synthesizeEnterAndAwaitLoad(url);

      providersManager.unregisterProvider(provider);
    });
  });
});

// UrlbarShared.RESULT_GROUP.HEURISTIC_OMNIBOX should be hidden.
add_task(async function omnibox() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    // Load an extension.
    let extension = ExtensionTestUtils.loadExtension({
      manifest: {
        omnibox: {
          keyword: "omniboxtest",
        },
      },
      background() {
        /* global browser */
        browser.omnibox.onInputEntered.addListener(() => {
          browser.test.sendMessage("onInputEntered");
        });
      },
    });
    await extension.startup();

    // Do a search using the omnibox keyword and check the hidden heuristic
    // result.
    let heuristic = await search({
      value: "omniboxtest foo",
      expectedGroup: UrlbarShared.RESULT_GROUP.HEURISTIC_OMNIBOX,
    });
    Assert.equal(
      heuristic.payload.keyword,
      "omniboxtest",
      "Heuristic keyword is correct"
    );

    // Press enter to verify the heuristic result is picked.
    let messagePromise = extension.awaitMessage("onInputEntered");
    EventUtils.synthesizeKey("KEY_Enter");
    await messagePromise;

    await extension.unload();
  });
});

// UrlbarShared.RESULT_GROUP.HEURISTIC_SEARCH_TIP should be shown.
add_task(async function searchTip() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.searchTips.test.ignoreShowLimits", true]],
  });
  await BrowserTestUtils.withNewTab(
    {
      gBrowser: window.gBrowser,
      url: "about:newtab",
      // `withNewTab` hangs waiting for about:newtab to load without this.
      waitForLoad: false,
    },
    async () => {
      await UrlbarTestUtils.promisePopupOpen(window, () => {});
      Assert.ok(true, "View opened");
      Assert.equal(UrlbarTestUtils.getResultCount(window), 1);
      let result = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
      Assert.equal(result.type, UrlbarShared.RESULT_TYPE.TIP);
      Assert.ok(result.heuristic);
      Assert.ok(UrlbarTestUtils.getSelectedElement(window), "Selection exists");
    }
  );
  await SpecialPowers.popPrefEnv();
});

// UrlbarShared.RESULT_GROUP.HEURISTIC_ENGINE_ALIAS should be hidden.
add_task(async function engineAlias() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await withVisits(async visitURLs => {
      // Add an engine with an alias.
      await withEngine({ keyword: "test" }, async () => {
        // Do a search using the alias and check the hidden heuristic result.
        // The heuristic will be HEURISTIC_FALLBACK, not HEURISTIC_ENGINE_ALIAS,
        // because two searches are performed and
        // `UrlbarTestUtils.promiseAutocompleteResultPopup` waits for both. The
        // first returns a HEURISTIC_ENGINE_ALIAS that triggers search mode and
        // then an immediate second search, which returns HEURISTIC_FALLBACK.
        let heuristic = await search({
          value: "test foo",
          expectedGroup: UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK,
        });
        Assert.equal(
          heuristic.payload.engine,
          "Example",
          "Heuristic engine is correct"
        );
        Assert.equal(
          heuristic.payload.query,
          "foo",
          "Heuristic query is correct"
        );
        await UrlbarTestUtils.assertSearchMode(window, {
          engineName: "Example",
          entry: "typed",
        });

        // Check the other visit results.
        await checkVisitResults(visitURLs);

        // Press enter to verify the heuristic result is loaded.
        await synthesizeEnterAndAwaitLoad("https://example.com/?q=foo");
      });
    });
  });
});

// UrlbarShared.RESULT_GROUP.HEURISTIC_BOOKMARK_KEYWORD should be hidden.
add_task(async function bookmarkKeyword() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await withVisits(async visitURLs => {
      // Add a bookmark with a keyword.
      let keyword = "bm";
      let bm = await PlacesUtils.bookmarks.insert({
        parentGuid: PlacesUtils.bookmarks.unfiledGuid,
        url: "http://example.com/?q=%s",
        title: "test",
      });
      await PlacesUtils.keywords.insert({ keyword, url: bm.url });

      // Do a search using the keyword and check the hidden heuristic result.
      let heuristic = await search({
        value: "bm foo",
        expectedGroup: UrlbarShared.RESULT_GROUP.HEURISTIC_BOOKMARK_KEYWORD,
      });
      Assert.equal(
        heuristic.payload.keyword,
        keyword,
        "Heuristic keyword is correct"
      );
      let heuristicURL = "http://example.com/?q=foo";
      Assert.equal(
        heuristic.payload.url,
        heuristicURL,
        "Heuristic URL is correct"
      );

      // Check the other visit results.
      await checkVisitResults(visitURLs);

      // Press enter to verify the heuristic result is loaded.
      await synthesizeEnterAndAwaitLoad(heuristicURL);

      await PlacesUtils.bookmarks.eraseEverything();
      await UrlbarTestUtils.formHistory.clear(window);
    });
  });
});

// UrlbarShared.RESULT_GROUP.HEURISTIC_AUTOFILL should be hidden.
add_task(async function autofill() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await withVisits(async visitURLs => {
      // Do a search that triggers autofill and check the hidden heuristic
      // result.
      let heuristic = await search({
        value: "ex",
        expectedGroup: UrlbarShared.RESULT_GROUP.HEURISTIC_AUTOFILL,
      });
      Assert.ok(heuristic.autofill, "Heuristic is autofill");
      let heuristicURL = "http://example.com/";
      Assert.equal(
        heuristic.payload.url,
        heuristicURL,
        "Heuristic URL is correct"
      );
      Assert.equal(gURLBar.value, "example.com/", "Input has been autofilled");

      // Check the other visit results.
      await checkVisitResults(visitURLs);

      // Press enter to verify the heuristic result is loaded.
      await synthesizeEnterAndAwaitLoad(heuristicURL);
    });
  });
});

// UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK with an unknown URL should be
// hidden.
add_task(async function fallback_unknownURL() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    // Do a search for an unknown URL and check the hidden heuristic result.
    let url = "http://example.com/unknown-url";
    let heuristic = await search({
      value: url,
      expectedGroup: UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK,
    });
    Assert.equal(heuristic.payload.url, url, "Heuristic URL is correct");

    // Press enter to verify the heuristic result is loaded.
    await synthesizeEnterAndAwaitLoad(url);
  });
});

// UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK with the search restriction token
// should be hidden.
add_task(async function fallback_searchRestrictionToken() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await withVisits(async visitURLs => {
      // Add a mock default engine so we don't hit the network.
      await withEngine({ makeDefault: true }, async () => {
        // Do a search with `?` and check the hidden heuristic result.
        let heuristic = await search({
          value: UrlbarShared.RESTRICT_TOKENS.SEARCH + " foo",
          expectedGroup: UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK,
        });
        Assert.equal(
          heuristic.payload.engine,
          "Example",
          "Heuristic engine is correct"
        );
        Assert.equal(
          heuristic.payload.query,
          "foo",
          "Heuristic query is correct"
        );
        await UrlbarTestUtils.assertSearchMode(window, {
          engineName: "Example",
          entry: "typed",
          restrictType: "symbol",
        });

        // Check the other visit results.
        await checkVisitResults(visitURLs);

        // Press enter to verify the heuristic result is loaded.
        await synthesizeEnterAndAwaitLoad("https://example.com/?q=foo");

        await UrlbarTestUtils.formHistory.clear(window);
      });
    });
  });
});

// UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK with a search string that falls
// back to a search result should be hidden.
add_task(async function fallback_search() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await withVisits(async visitURLs => {
      // Add a mock default engine so we don't hit the network.
      await withEngine({ makeDefault: true }, async () => {
        // Do a search and check the hidden heuristic result.
        let heuristic = await search({
          value: "foo",
          expectedGroup: UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK,
        });
        Assert.equal(
          heuristic.payload.engine,
          "Example",
          "Heuristic engine is correct"
        );
        Assert.equal(
          heuristic.payload.query,
          "foo",
          "Heuristic query is correct"
        );

        // Check the other visit results.
        await checkVisitResults(visitURLs);

        // Press enter to verify the heuristic result is loaded.
        await synthesizeEnterAndAwaitLoad("https://example.com/?q=foo");

        await UrlbarTestUtils.formHistory.clear(window);
      });
    });
  });
});

// Picking a non-heuristic result should work correctly (and not pick the
// heuristic).
add_task(async function pickNonHeuristic() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await withVisits(async visitURLs => {
      // Do a search that triggers autofill and check the hidden heuristic
      // result.
      let heuristic = await search({
        value: "ex",
        expectedGroup: UrlbarShared.RESULT_GROUP.HEURISTIC_AUTOFILL,
      });
      Assert.ok(heuristic.autofill, "Heuristic is autofill");
      Assert.equal(
        heuristic.payload.url,
        "http://example.com/",
        "Heuristic URL is correct"
      );

      // Pick the first visit result.
      Assert.notEqual(
        heuristic.payload.url,
        visitURLs[0],
        "Sanity check: Heuristic and first results have different URLs"
      );
      EventUtils.synthesizeKey("KEY_ArrowDown");
      await synthesizeEnterAndAwaitLoad(visitURLs[0]);
    });
  });
});

/**
 * Adds `maxRichResults` visits, calls your callback, and clears history. We add
 * `maxRichResults` visits to verify that the view correctly contains the
 * maximum number of results when the heuristic is hidden.
 *
 * @param {Function} callback
 *   The callback to call after adding visits. Can be async
 */
async function withVisits(callback) {
  let urls = [];
  for (let i = 0; i < UrlbarPrefs.get("maxRichResults"); i++) {
    urls.push("http://example.com/foo/" + i);
  }

  let typedVisits = urls.map(url => {
    return {
      url,
      transition: PlacesUtils.history.TRANSITION_TYPED,
    };
  });
  await PlacesTestUtils.addVisits(typedVisits);

  // The URLs will appear in the view in reverse order so that newer visits are
  // first. Reverse the array now so callers to `checkVisitResults` or
  // `checkVisitResults` itself doesn't need to do it.
  urls.reverse();

  await callback(urls);
  await PlacesUtils.history.clear();
}

/**
 * Adds a search engine, calls your callback, and removes the engine.
 *
 * @param {object} options
 *   Options object
 * @param {string} [options.keyword]
 *   The keyword/alias for the engine.
 * @param {boolean} [options.makeDefault]
 *   Whether to make the engine default.
 * @param {Function} callback
 *   The callback to call after changing the default search engine. Can be async
 */
async function withEngine(
  { keyword = undefined, makeDefault = false },
  callback
) {
  await SearchTestUtils.installSearchExtension({ keyword });
  let engine = SearchService.getEngineByName("Example");
  let originalEngine;
  if (makeDefault) {
    originalEngine = await SearchService.getDefault();
    await SearchService.setDefault(engine, SearchService.CHANGE_REASON.UNKNOWN);
  }
  await callback();
  if (originalEngine) {
    await SearchService.setDefault(
      originalEngine,
      SearchService.CHANGE_REASON.UNKNOWN
    );
  }
  await SearchService.removeEngine(engine);
}

/**
 * Asserts the view contains visit results with the given URLs.
 *
 * @param {Array} expectedURLs
 *   The expected urls.
 */
async function checkVisitResults(expectedURLs) {
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    expectedURLs.length,
    "The view has other results"
  );
  for (let i = 0; i < expectedURLs.length; i++) {
    let result = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    Assert.equal(
      result.type,
      UrlbarShared.RESULT_TYPE.URL,
      "Other result type is correct at index " + i
    );
    Assert.equal(
      result.source,
      UrlbarShared.RESULT_SOURCE.HISTORY,
      "Other result source is correct at index " + i
    );
    Assert.equal(
      result.url,
      expectedURLs[i],
      "Other result URL is correct at index " + i
    );
  }
}

/**
 * Performs a search and makes some basic assertions under the assumption that
 * the heuristic should be hidden.
 *
 * @param {object} options
 *   Options object
 * @param {string} options.value
 *   The search string.
 * @param {UrlbarShared.RESULT_GROUP} options.expectedGroup
 *   The expected result group of the hidden heuristic.
 * @returns {UrlbarResult}
 *   The hidden heuristic result.
 */
async function search({ value, expectedGroup }) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    waitForFocus,
    value,
    fireInputEvent: true,
  });

  // _resultForCurrentValue should be the hidden heuristic result.
  let { _resultForCurrentValue: result } = gURLBar;
  Assert.ok(result, "_resultForCurrentValue is defined");
  Assert.ok(result.heuristic, "_resultForCurrentValue.heuristic is true");
  Assert.equal(
    UrlbarShared.getResultGroup(result),
    expectedGroup,
    "_resultForCurrentValue has expected group"
  );

  Assert.ok(!UrlbarTestUtils.getSelectedElement(window), "No selection exists");

  return result;
}

/**
 * Synthesizes the enter key and waits for a load in the current tab.
 *
 * @param {string} expectedURL
 *   The URL that should load.
 */
async function synthesizeEnterAndAwaitLoad(expectedURL) {
  let loadPromise = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    expectedURL
  );
  EventUtils.synthesizeKey("KEY_Enter");
  await loadPromise;
  await PlacesUtils.history.clear();
}
