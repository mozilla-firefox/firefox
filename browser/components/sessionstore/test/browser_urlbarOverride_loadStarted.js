/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Session store records a started load of the address bar value override
// only for a load of that value, so that restoring the session doesn't load
// text the user typed when the page was loading something else.

const SLOW_URL =
  "http://mochi.test:8888/browser/browser/components/" +
  "sessionstore/test/browser_sessionHistory_slow.sjs";

const { TabStateCache } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateCache.sys.mjs"
);

function waitForLoadStarted(browser) {
  return TestUtils.waitForCondition(
    () => browser.loadStartedSinceUrlbarOverride,
    "Waiting for the load to start"
  );
}

function typeInAddressBar(text) {
  gURLBar.focus();
  gURLBar.value = text.slice(0, -1);
  EventUtils.sendString(text.slice(-1));
}

add_task(async function test_other_load_after_typing() {
  await BrowserTestUtils.withNewTab("https://example.com/", async browser => {
    typeInAddressBar("foo");
    is(browser.urlbarOverrideValue, "foo", "The typed value is the override");

    let loadStarted = waitForLoadStarted(browser);
    await SpecialPowers.spawn(browser, [SLOW_URL], url => {
      content.location = url;
    });
    await loadStarted;

    let tabData = TabState.collect(gBrowser.selectedTab);
    is(tabData.urlbarOverrideValue, "foo", "Session store saved the value");
    ok(
      !("urlbarOverrideLoadStarted" in tabData),
      "Session store didn't record a load of the value"
    );
    gURLBar.view.close();
  });
});

add_task(async function test_addressbar_load() {
  await BrowserTestUtils.withNewTab("https://example.com/", async browser => {
    typeInAddressBar(SLOW_URL);
    let loadStarted = waitForLoadStarted(browser);
    EventUtils.synthesizeKey("KEY_Enter");
    await loadStarted;

    let tabData = TabState.collect(gBrowser.selectedTab);
    is(tabData.urlbarOverrideValue, SLOW_URL, "Session store saved the value");
    is(
      tabData.urlbarOverrideLoadStarted,
      true,
      "Session store recorded a load of the value"
    );
  });
});

add_task(async function test_addTab_load() {
  let tab = BrowserTestUtils.addTab(gBrowser, SLOW_URL);
  await waitForLoadStarted(tab.linkedBrowser);

  let tabData = TabState.collect(tab);
  is(tabData.urlbarOverrideValue, SLOW_URL, "Session store saved the value");
  is(
    tabData.urlbarOverrideLoadStarted,
    true,
    "Session store recorded a load of the value"
  );
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_discard_before_caching() {
  for (let { url, typed, loadStarted } of [
    { url: "about:blank", typed: "foo", loadStarted: undefined },
    { url: SLOW_URL, loadStarted: true },
  ]) {
    let tab = BrowserTestUtils.addTab(gBrowser, url);
    let browser = tab.linkedBrowser;
    if (typed) {
      browser.urlbarOverride = { value: typed, typed: true };
    }
    let value = browser.urlbarOverrideValue;
    ok(value, `${url} has an override`);
    ok(!TabStateCache.get(browser.permanentKey), "No tab state is cached");

    ok(gBrowser.discardBrowser(tab), "The tab is discarded");
    let cached = TabStateCache.get(browser.permanentKey);
    is(cached.urlbarOverrideValue, value, "The value is cached");
    is(
      cached.urlbarOverrideLoadStarted,
      loadStarted,
      "A load is cached only for a value that was loading"
    );
    BrowserTestUtils.removeTab(tab);
  }
});
