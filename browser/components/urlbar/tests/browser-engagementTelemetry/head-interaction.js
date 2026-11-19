/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head.js */

ChromeUtils.defineESModuleGetters(this, {
  CUSTOM_SEARCH_SHORTCUTS:
    "moz-src:///toolkit/components/search/SearchShortcuts.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  SEARCH_SHORTCUTS:
    "moz-src:///toolkit/components/search/SearchShortcuts.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
});

async function doTopsitesTest({ trigger, assert }) {
  await doTest(async () => {
    let cleanupTopSites = await addTopSites("https://example.com/");

    await showResultByArrowDown();
    await selectRowByURL("https://example.com/");

    await trigger();
    await assert();

    await cleanupTopSites();
  });
}

async function doTopsitesSearchTest({ trigger, assert }) {
  await doTest(async browser => {
    let extension = await SearchTestUtils.installSearchExtension(
      {
        name: "MozSearch",
        keyword: "@test",
        search_url: "https://example.com/",
        search_url_get_params: "q={searchTerms}",
      },
      { skipUnload: true }
    );

    // Fresh profiles come with an empty set of pinned websites (pref doesn't
    // exist). Search shortcut topsites make this test more complicated because
    // the feature pins a new website on startup. Behaviour can vary when running
    // with --verify so it's more predictable to clear pins entirely.
    Services.prefs.clearUserPref("browser.newtabpage.pinned");
    NewTabUtils.pinnedLinks.resetCache();

    let entry = {
      keyword: "@test",
      shortURL: "example",
      url: "https://example.com/",
    };

    // The array is used to identify sites that should be converted to
    // a Top Site.
    let searchShortcuts = JSON.parse(JSON.stringify(SEARCH_SHORTCUTS));
    SEARCH_SHORTCUTS.push(entry);

    // TopSitesFeed takes a list of app provided engines and determine if the
    // engine containing an alias that matches a keyword inside of this array.
    // If so, the list of search shortcuts in the store will be updated.
    let customSearchShortcuts = JSON.parse(
      JSON.stringify(CUSTOM_SEARCH_SHORTCUTS)
    );
    CUSTOM_SEARCH_SHORTCUTS.push(entry);

    // TopSitesFeed only allows app provided engines to be included as
    // search shortcuts.
    // eslint-disable-next-line mozilla/valid-lazy
    let sandbox = lazy.sinon.createSandbox();
    sandbox
      .stub(SearchService, "getAppProvidedEngines")
      .resolves([{ aliases: ["@test"] }]);

    let siteToPin = {
      url: "https://example.com",
      label: "@test",
      searchTopSite: true,
    };
    NewTabUtils.pinnedLinks.pin(siteToPin, 0);

    let cleanupTopSites = await updateTopSites(sites => {
      return sites && sites[0] && sites[0].url == "https://example.com";
    }, true);

    BrowserTestUtils.startLoadingURIString(browser, "about:newtab");
    await BrowserTestUtils.browserStopped(browser, "about:newtab");

    await BrowserTestUtils.synthesizeMouseAtCenter(
      ".search-shortcut .top-site-button",
      {},
      gBrowser.selectedBrowser
    );

    EventUtils.synthesizeKey("x");
    await UrlbarTestUtils.promiseSearchComplete(window);

    await trigger();
    await assert();

    // Clean up.
    NewTabUtils.pinnedLinks.unpin(siteToPin);
    SEARCH_SHORTCUTS.pop();
    CUSTOM_SEARCH_SHORTCUTS.pop();
    // Sanity check to ensure we're leaving the shortcuts in their default state.
    Assert.deepEqual(
      searchShortcuts,
      SEARCH_SHORTCUTS,
      "SEARCH_SHORTCUTS values"
    );
    Assert.deepEqual(
      customSearchShortcuts,
      CUSTOM_SEARCH_SHORTCUTS,
      "CUSTOM_SEARCH_SHORTCUTS values"
    );
    sandbox.restore();
    await cleanupTopSites();
    Services.prefs.clearUserPref("browser.newtabpage.pinned");
    NewTabUtils.pinnedLinks.resetCache();
    await extension.unload();
  });
}

async function doTypedTest({ trigger, assert }) {
  await doTest(async () => {
    await openPopup("x");

    await trigger();
    await assert();
  });
}

async function doTypedWithResultsPopupTest({ trigger, assert }) {
  await doTest(async () => {
    let cleanupTopSites = await addTopSites("https://example.org/");
    await showResultByArrowDown();
    EventUtils.synthesizeKey("x");
    await UrlbarTestUtils.promiseSearchComplete(window);

    await trigger();
    await assert();

    await cleanupTopSites();
  });
}

async function doPastedTest({ trigger, assert }) {
  await doTest(async () => {
    await doPaste("www.example.com");

    await trigger();
    await assert();
  });
}

async function doPastedWithResultsPopupTest({ trigger, assert }) {
  await doTest(async () => {
    let cleanupTopSites = await addTopSites("https://example.org/");
    await showResultByArrowDown();
    await doPaste("x");

    await trigger();
    await assert();

    await cleanupTopSites();
  });
}

async function doReturnedRestartedRefinedTest({ trigger, assert }) {
  const testData = [
    {
      firstInput: "x",
      // Just move the focus to the URL bar after blur.
      secondInput: null,
      expected: "returned",
    },
    {
      firstInput: "x",
      secondInput: "x",
      expected: "returned",
    },
    {
      firstInput: "x",
      secondInput: "y",
      expected: "restarted",
    },
    {
      firstInput: "x",
      secondInput: "x y",
      expected: "refined",
    },
    {
      firstInput: "x y",
      secondInput: "x",
      expected: "refined",
    },
  ];

  for (const { firstInput, secondInput, expected } of testData) {
    await doTest(async () => {
      await openPopup(firstInput);
      await doBlur();

      await UrlbarTestUtils.promisePopupOpen(window, () => {
        document.getElementById("Browser:OpenLocation").doCommand();
      });
      if (secondInput) {
        for (let i = 0; i < secondInput.length; i++) {
          EventUtils.synthesizeKey(secondInput.charAt(i));
        }
      }
      await UrlbarTestUtils.promiseSearchComplete(window);

      await trigger();
      await assert(expected);
    });
  }
}

async function doPersistedSearchTermsTest({ trigger, assert }) {
  await doTest(async () => {
    await openPopup("x");
    await doEnter();

    await openPopup("x");

    await trigger();
    await assert();
  });
}

async function doPersistedSearchTermsRestartedRefinedTest({
  enabled,
  trigger,
  assert,
}) {
  const testData = [
    {
      firstInput: "x",
      // Just move the focus to the URL bar after engagement.
      secondInput: null,
      expected: enabled ? "persisted_search_terms" : "topsites",
    },
    {
      firstInput: "x",
      secondInput: "x",
      expected: enabled ? "persisted_search_terms" : "typed",
    },
    {
      firstInput: "x",
      secondInput: "y",
      expected: enabled ? "persisted_search_terms_restarted" : "typed",
    },
    {
      firstInput: "x",
      secondInput: "x y",
      expected: enabled ? "persisted_search_terms_refined" : "typed",
    },
    {
      firstInput: "x y",
      secondInput: "x",
      expected: enabled ? "persisted_search_terms_refined" : "typed",
    },
  ];

  for (const { firstInput, secondInput, expected } of testData) {
    await doTest(async () => {
      let cleanupTopSites = await addTopSites("https://example.com/");
      await openPopup(firstInput);
      await doEnter();

      await UrlbarTestUtils.promisePopupOpen(window, () => {
        EventUtils.synthesizeKey("l", { accelKey: true });
      });
      if (secondInput) {
        for (let i = 0; i < secondInput.length; i++) {
          EventUtils.synthesizeKey(secondInput.charAt(i));
        }
      }
      await UrlbarTestUtils.promiseSearchComplete(window);

      await trigger();
      await assert(expected);

      await cleanupTopSites();
    });
  }
}

async function doPersistedSearchTermsRestartedRefinedViaAbandonmentTest({
  enabled,
  trigger,
  assert,
}) {
  const testData = [
    {
      firstInput: "x",
      // Just move the focus to the URL bar after blur.
      secondInput: null,
      expected: enabled ? "persisted_search_terms" : "returned",
    },
    {
      firstInput: "x",
      secondInput: "x",
      expected: enabled ? "persisted_search_terms" : "returned",
    },
    {
      firstInput: "x",
      secondInput: "y",
      expected: enabled ? "persisted_search_terms_restarted" : "restarted",
    },
    {
      firstInput: "x",
      secondInput: "x y",
      expected: enabled ? "persisted_search_terms_refined" : "refined",
    },
    {
      firstInput: "x y",
      secondInput: "x",
      expected: enabled ? "persisted_search_terms_refined" : "refined",
    },
  ];

  for (const { firstInput, secondInput, expected } of testData) {
    await doTest(async () => {
      await openPopup("any search");
      await doEnter();

      await openPopup(firstInput);
      await doBlur();

      await UrlbarTestUtils.promisePopupOpen(window, () => {
        EventUtils.synthesizeKey("l", { accelKey: true });
      });
      if (secondInput) {
        for (let i = 0; i < secondInput.length; i++) {
          EventUtils.synthesizeKey(secondInput.charAt(i));
        }
      }
      await UrlbarTestUtils.promiseSearchComplete(window);

      await trigger();
      await assert(expected);
    });
  }
}
