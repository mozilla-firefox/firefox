/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

ChromeUtils.defineESModuleGetters(this, {
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

ChromeUtils.defineLazyGetter(this, "UrlbarTestUtils", () => {
  const { UrlbarTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/UrlbarTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

/**
 * Clicks the given link and checks this opens the given URI in the new tab.
 *
 * This function does not return to the previous page.
 */
async function testLinkOpensUrl({ win, tab, elementId, expectedUrl }) {
  let loadedPromise = BrowserTestUtils.waitForNewTab(win.gBrowser, url =>
    url.startsWith(expectedUrl)
  );
  await SpecialPowers.spawn(tab, [elementId], async function (elemId) {
    content.document.getElementById(elemId).click();
  });
  await loadedPromise;
  is(
    win.gBrowser.selectedBrowser.currentURI.spec,
    expectedUrl,
    `Clicking ${elementId} opened ${expectedUrl} in the same tab.`
  );
}

let expectedEngineAlias;
let expectedIconURL;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.search.separatePrivateDefault.enabled", true],
      // Enable suggestions in this test. Otherwise, the behaviour of the
      // content search box changes.
      ["browser.search.suggest.enabled", true],
    ],
  });

  const originalPrivateDefault = await SearchService.getDefaultPrivate();
  // We have to use a built-in engine as we are currently hard-coding the aliases.
  const privateEngine = await SearchService.getEngineByName("DuckDuckGo");
  await SearchService.setDefaultPrivate(
    privateEngine,
    SearchService.CHANGE_REASON.UNKNOWN
  );
  expectedEngineAlias = privateEngine.aliases[0];
  expectedIconURL = await privateEngine.getIconURL();

  registerCleanupFunction(async () => {
    await SearchService.setDefaultPrivate(
      originalPrivateDefault,
      SearchService.CHANGE_REASON.UNKNOWN
    );
  });
});

/**
 * Tests the private-browsing-myths link in "about:privatebrowsing".
 */
add_task(async function test_myths_link() {
  Services.prefs.setCharPref("app.support.baseURL", "https://example.com/");
  registerCleanupFunction(function () {
    Services.prefs.clearUserPref("app.support.baseURL");
  });

  let { win, tab } = await openAboutPrivateBrowsing();

  await testLinkOpensUrl({
    win,
    tab,
    elementId: "private-browsing-myths",
    expectedUrl: "https://example.com/private-browsing-myths",
  });

  await BrowserTestUtils.closeWindow(win);
});

async function urlBarHasHiddenFocus(win) {
  return TestUtils.waitForCondition(() => {
    return win.gURLBar.focused && !win.gURLBar.hasAttribute("focused");
  }, "Urlbar has hidden focus");
}

function urlBarHasNormalFocus(win) {
  return win.gURLBar.hasAttribute("focused");
}

/**
 * Tests that we have the correct icon displayed.
 */
add_task(async function test_search_icon_legacy() {
  let { win, tab } = await openAboutPrivateBrowsing();

  await SpecialPowers.spawn(tab, [expectedIconURL], async function (iconURL) {
    let computedStyle = content.window.getComputedStyle(content.document.body);
    await ContentTaskUtils.waitForCondition(
      () =>
        computedStyle
          .getPropertyValue("--newtab-search-icon")
          .startsWith("url"),
      "Search Icon should get set."
    );

    if (iconURL.startsWith("blob:")) {
      // We don't check the data here as `browser_contentSearch.js` performs
      // those checks.
      Assert.ok(
        computedStyle
          .getPropertyValue("--newtab-search-icon")
          .startsWith("url(blob:"),
        "Should have a blob URL for the logo"
      );
    } else {
      Assert.equal(
        computedStyle.getPropertyValue("--newtab-search-icon"),
        `url(${iconURL})`,
        "Should have the correct icon URL for the logo"
      );
    }
  });

  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests that the search handoff button in about:privatebrowsing shows the
 * current engine's icon (via --newtab-search-icon) rather than the searchglass.
 */
add_task(async function test_search_icon() {
  let { win, tab } = await openAboutPrivateBrowsing();

  await SpecialPowers.spawn(tab, [], async function () {
    let handoffUI = content.document.querySelector("content-search-handoff-ui");
    let btn = handoffUI.shadowRoot.querySelector(".search-handoff-button");
    await handoffUI.updateComplete;

    const searchGlass = `url("chrome://global/skin/icons/search-glass.svg")`;

    await ContentTaskUtils.waitForCondition(
      () =>
        content.window.getComputedStyle(btn).backgroundImage.startsWith("url("),
      "Search icon should get set."
    );
    let bg = content.window.getComputedStyle(btn).backgroundImage;
    isnot(bg, searchGlass, "The searchglass icon is not pinned");
    ok(
      /^url\("(blob:|chrome:)/.test(bg),
      "Handoff button shows the engine icon"
    );
  });

  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests the search hand-off on character keydown in "about:privatebrowsing".
 */
add_task(async function test_search_handoff_on_keydown() {
  let { win, tab } = await openAboutPrivateBrowsing();

  await SpecialPowers.spawn(tab, [], async function () {
    let handoffUI = content.document.querySelector("content-search-handoff-ui");
    let btn = handoffUI.shadowRoot.querySelector(".search-handoff-button");
    btn.click();
    await handoffUI.updateComplete;
    ok(
      handoffUI.hasAttribute("fakefocus"),
      "in-content search has focus styles"
    );
  });
  await urlBarHasHiddenFocus(win);

  // Expect two searches, one to enter search mode and then another in search
  // mode.
  let searchPromise = UrlbarTestUtils.promiseSearchComplete(win);

  await new Promise(r => EventUtils.synthesizeKey("f", {}, win, r));
  await SpecialPowers.spawn(tab, [], async function () {
    ok(
      content.document
        .querySelector("content-search-handoff-ui")
        .hasAttribute("disabled"),
      "in-content search is disabled"
    );
  });
  await searchPromise;
  ok(urlBarHasNormalFocus(win), "Urlbar has normal focus");
  is(win.gURLBar.value, "f", "url bar has search text");

  // Close the popup.
  await UrlbarTestUtils.promisePopupClose(win);

  // Hitting ESC should reshow the in-content search
  await new Promise(r => EventUtils.synthesizeKey("KEY_Escape", {}, win, r));
  await SpecialPowers.spawn(tab, [], async function () {
    ok(
      !content.document
        .querySelector("content-search-handoff-ui")
        .hasAttribute("disabled"),
      "in-content search is not disabled"
    );
  });

  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests the search hand-off on composition start in "about:privatebrowsing".
 */
add_task(async function test_search_handoff_on_composition_start() {
  let { win, tab } = await openAboutPrivateBrowsing();

  await SpecialPowers.spawn(tab, [], async function () {
    let btn = content.document
      .querySelector("content-search-handoff-ui")
      .shadowRoot.querySelector(".search-handoff-button");
    btn.click();
  });
  await urlBarHasHiddenFocus(win);
  await new Promise(r =>
    EventUtils.synthesizeComposition({ type: "compositionstart" }, win, r)
  );
  ok(urlBarHasNormalFocus(win), "Urlbar has normal focus");

  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests the search hand-off on paste in "about:privatebrowsing".
 */
add_task(async function test_search_handoff_on_paste() {
  let { win, tab } = await openAboutPrivateBrowsing();

  await SpecialPowers.spawn(tab, [], async function () {
    content.document
      .querySelector("content-search-handoff-ui")
      .shadowRoot.querySelector(".search-handoff-button")
      .click();
  });
  await urlBarHasHiddenFocus(win);
  var helper = SpecialPowers.Cc[
    "@mozilla.org/widget/clipboardhelper;1"
  ].getService(SpecialPowers.Ci.nsIClipboardHelper);
  helper.copyString("words");

  // Expect two searches, one to enter search mode and then another in search
  // mode.
  let searchPromise = UrlbarTestUtils.promiseSearchComplete(win);

  await new Promise(r =>
    EventUtils.synthesizeKey("v", { accelKey: true }, win, r)
  );

  await searchPromise;

  ok(urlBarHasNormalFocus(win), "Urlbar has normal focus");
  is(win.gURLBar.value, "words", "Urlbar has search text");

  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests that handoff enters search mode when suggestions are disabled.
 */
add_task(async function test_search_handoff_search_mode() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.suggest.searches", false]],
  });

  let { win, tab } = await openAboutPrivateBrowsing();

  await SpecialPowers.spawn(tab, [], async function () {
    let handoffUI = content.document.querySelector("content-search-handoff-ui");
    let btn = handoffUI.shadowRoot.querySelector(".search-handoff-button");
    btn.click();
    await handoffUI.updateComplete;
    ok(
      handoffUI.hasAttribute("fakefocus"),
      "in-content search has focus styles"
    );
  });
  await urlBarHasHiddenFocus(win);

  // Expect two searches, one to enter search mode and then another in search
  // mode.
  let searchPromise = UrlbarTestUtils.promiseSearchComplete(win);

  await new Promise(r => EventUtils.synthesizeKey("f", {}, win, r));
  await SpecialPowers.spawn(tab, [], async function () {
    await ContentTaskUtils.waitForCondition(() => {
      return content.document
        .querySelector("content-search-handoff-ui")
        .hasAttribute("disabled");
    }, "in-content search is disabled");
  });
  await searchPromise;
  ok(urlBarHasNormalFocus(win), "Urlbar has normal focus");
  await UrlbarTestUtils.assertSearchMode(win, {
    engineName: "DuckDuckGo",
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
    entry: "handoff",
  });
  is(win.gURLBar.value, "f", "url bar has search text");

  // Close the popup.
  await UrlbarTestUtils.exitSearchMode(win);
  await UrlbarTestUtils.promisePopupClose(win);

  // Hitting ESC should reshow the in-content search
  await new Promise(r => EventUtils.synthesizeKey("KEY_Escape", {}, win, r));
  await SpecialPowers.spawn(tab, [], async function () {
    ok(
      !content.document
        .querySelector("content-search-handoff-ui")
        .shadowRoot.querySelector(".search-handoff-button")
        .hasAttribute("disabled"),
      "in-content search is not disabled"
    );
  });

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});
