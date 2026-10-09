/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that search mode remains active or is exited when setURI is called,
 * depending on the situation.
 */

"use strict";

// Opens a new tab, does a search, enters search mode, and then manually calls
// setURI.  Uses a variety of initial URLs, search strings, and setURI arguments
// in order to hit different branches in setURI.  Search mode should remain
// active or be exited as appropriate.
add_task(async function setURI() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.scotchBonnet.enableOverride", false],
      // Tests get no default Top Sites, and doSetURITest opens the view on an
      // empty string, which would otherwise leave it closed with no results.
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });
  for (let test of [
    // initialURL, searchString, url, expectSearchMode

    ["about:blank", "", null, true],
    ["about:blank", "", "about:blank", true],
    ["about:blank", "", "http://www.example.com/", true],

    ["about:blank", "about:blank", null, false],
    ["about:blank", "about:blank", "about:blank", false],
    ["about:blank", "about:blank", "http://www.example.com/", false],

    ["about:blank", "http://www.example.com/", null, true],
    ["about:blank", "http://www.example.com/", "about:blank", true],
    ["about:blank", "http://www.example.com/", "http://www.example.com/", true],

    ["about:blank", "not a URL", null, true],
    ["about:blank", "not a URL", "about:blank", true],
    ["about:blank", "not a URL", "http://www.example.com/", true],

    ["http://www.example.com/", "", null, true],
    ["http://www.example.com/", "", "about:blank", true],
    ["http://www.example.com/", "", "http://www.example.com/", true],

    ["http://www.example.com/", "about:blank", null, false],
    ["http://www.example.com/", "about:blank", "about:blank", false],
    [
      "http://www.example.com/",
      "about:blank",
      "http://www.example.com/",
      false,
    ],

    ["http://www.example.com/", "http://www.example.com/", null, true],
    ["http://www.example.com/", "http://www.example.com/", "about:blank", true],
    [
      "http://www.example.com/",
      "http://www.example.com/",
      "http://www.example.com/",
      true,
    ],

    ["http://www.example.com/", "not a URL", null, true],
    ["http://www.example.com/", "not a URL", "about:blank", true],
    ["http://www.example.com/", "not a URL", "http://www.example.com/", true],
  ]) {
    await doSetURITest(...test);
  }
});

async function doSetURITest(initialURL, searchString, url, expectSearchMode) {
  info(
    "doSetURITest with args: " +
      JSON.stringify({
        initialURL,
        searchString,
        url,
        expectSearchMode,
      })
  );

  await BrowserTestUtils.withNewTab(initialURL, async () => {
    if (searchString) {
      // Do a search with the search string.
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: searchString,
        fireInputEvent: true,
      });
    } else {
      // Open top sites.
      await UrlbarTestUtils.promisePopupOpen(window, () => {
        document.getElementById("Browser:OpenLocation").doCommand();
      });
    }

    // Enter search mode and close the view.
    await UrlbarTestUtils.enterSearchMode(window, {
      source: UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
    });
    await UrlbarTestUtils.promisePopupClose(window);
    Assert.strictEqual(
      gBrowser.selectedBrowser.userTypedValue,
      searchString,
      `userTypedValue should be ${searchString}`
    );

    // Call setURI.
    let uri = url ? Services.io.newURI(url) : null;
    gURLBar.setURI({ uri });

    await UrlbarTestUtils.assertSearchMode(
      window,
      !expectSearchMode
        ? null
        : {
            source: UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
            entry: "oneoff",
          }
    );

    gURLBar.handleRevert();
    await UrlbarTestUtils.assertSearchMode(window, null);
  });
}
