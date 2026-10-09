/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that search mode is exited after clicking a link and loading a page in
 * the current tab.
 */

"use strict";

const LINK_PAGE_URL =
  "http://mochi.test:8888/browser/browser/components/urlbar/tests/browser/dummy_page.html";

// Opens a new tab containing a link, enters search mode, and clicks the link.
// Uses a variety of search strings and link hrefs in order to hit different
// branches in setURI.  Search mode should be exited in all cases, and the href
// in the link should be opened.
add_task(async function clickLink() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.scotchBonnet.enableOverride", false],
      // Tests get no default Top Sites, and the view is opened on an empty
      // string here, which would otherwise leave it closed with no results.
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });
  for (let test of [
    // searchString, href to use in the link
    [LINK_PAGE_URL, LINK_PAGE_URL],
    [LINK_PAGE_URL, "http://www.example.com/"],
    ["test", LINK_PAGE_URL],
    ["test", "http://www.example.com/"],
    [null, LINK_PAGE_URL],
    [null, "http://www.example.com/"],
  ]) {
    await doClickLinkTest(...test);
  }
});

async function doClickLinkTest(searchString, href) {
  info(
    "doClickLinkTest with args: " +
      JSON.stringify({
        searchString,
        href,
      })
  );

  await BrowserTestUtils.withNewTab(LINK_PAGE_URL, async () => {
    if (searchString) {
      // Do a search with the search string.
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: searchString,
        fireInputEvent: true,
      });
      Assert.ok(
        gBrowser.selectedBrowser.userTypedValue,
        "userTypedValue should be defined"
      );
    } else {
      // Open top sites.
      await UrlbarTestUtils.promisePopupOpen(window, () => {
        document.getElementById("Browser:OpenLocation").doCommand();
      });
      Assert.strictEqual(
        gBrowser.selectedBrowser.userTypedValue,
        null,
        "userTypedValue should be null"
      );
    }

    // Enter search mode and then close the popup so we can click the link in
    // the page.
    await UrlbarTestUtils.enterSearchMode(window, {
      source: UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
    });
    await UrlbarTestUtils.promisePopupClose(window);
    await UrlbarTestUtils.assertSearchMode(window, {
      source: UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
      entry: "oneoff",
    });

    // Add a link to the page and click it.
    let loadPromise = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
    await SpecialPowers.spawn(gBrowser.selectedBrowser, [href], async cHref => {
      let link = this.content.document.createElement("a");
      link.textContent = "Click me";
      link.href = cHref;
      this.content.document.body.append(link);
      link.click();
    });
    await loadPromise;
    Assert.equal(
      gBrowser.currentURI.spec,
      href,
      "Should have loaded the href URL"
    );

    await UrlbarTestUtils.assertSearchMode(window, null);
  });
}
