/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

add_task(async function test_blank() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async function (browser) {
      BrowserTestUtils.startLoadingURIString(browser, "https://example.com/");
      await BrowserTestUtils.browserLoaded(browser);
      ok(!gBrowser.canGoBack, "about:blank wasn't added to session history");
    }
  );
});

add_task(async function test_newtab() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async function (browser) {
      // Can't load it directly because that'll use a preloaded tab if present.
      let stopped = BrowserTestUtils.browserStopped(browser, "about:newtab");
      BrowserTestUtils.startLoadingURIString(browser, "about:newtab");
      await stopped;

      stopped = BrowserTestUtils.browserStopped(
        browser,
        "https://example.com/"
      );
      BrowserTestUtils.startLoadingURIString(browser, "https://example.com/");
      await stopped;

      // This makes sure the parent process has the most up-to-date notion
      // of the tab's session history.
      await TabStateFlusher.flush(browser);

      let tab = gBrowser.getTabForBrowser(browser);
      let tabState = JSON.parse(SessionStore.getTabState(tab));
      Assert.equal(
        tabState.entries.length,
        2,
        "We should have 2 entries in the session history."
      );

      Assert.equal(
        tabState.entries[0].url,
        "about:newtab",
        "about:newtab should be the first entry."
      );

      Assert.ok(gBrowser.canGoBack, "Should be able to browse back.");
    }
  );
});

// Test for bug 1676492, when newtab shows a blank page, don't add it to SH
add_task(async function test_blank_newtab() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtabpage.enabled", false]],
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:newtab" },
    async function (browser) {
      const loaded = BrowserTestUtils.browserLoaded(browser);
      BrowserTestUtils.startLoadingURIString(browser, "https://example.com/");
      await loaded;

      await TabStateFlusher.flush(browser);

      const tab = gBrowser.getTabForBrowser(browser);
      const tabState = JSON.parse(SessionStore.getTabState(tab));

      Assert.equal(tabState.entries.length, 1, "Should only have one entry");
      Assert.equal(
        tabState.entries[0].url,
        "https://example.com/",
        "Should have example.com SH entry"
      );
      Assert.ok(!gBrowser.canGoBack, "Should not be able to go back");
    }
  );

  await SpecialPowers.popPrefEnv();
});
