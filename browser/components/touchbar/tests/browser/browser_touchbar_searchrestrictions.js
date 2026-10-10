/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

ChromeUtils.defineESModuleGetters(this, {
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarUtils: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "UrlbarTestUtils", () => {
  const { UrlbarTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/UrlbarTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

XPCOMUtils.defineLazyServiceGetter(
  this,
  "TouchBarHelper",
  "@mozilla.org/widget/touchbarhelper;1",
  Ci.nsITouchBarHelper
);

/**
 * Tests the search restriction buttons in the Touch Bar.
 */

/**
 * @param {object} options
 * @param {string} options.input
 *   The value to be inserted in the Urlbar.
 * @param {UrlbarShared.RESTRICT_TOKENS} options.token
 *   A restriction token corresponding to a Touch Bar button.
 */
async function searchAndCheckState({ input, token }) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: input,
  });
  input = input.trimStart();
  if (Object.values(UrlbarShared.RESTRICT_TOKENS).includes(input[0])) {
    input = input.slice(1).trimStart();
  }
  let searchMode = gURLBar.searchModeForToken(token);
  let expectedValue = searchMode ? input : `${token} ${input}`;
  TouchBarHelper.insertRestrictionInUrlbar(token);

  if (searchMode) {
    searchMode.entry = "touchbar";
    await UrlbarTestUtils.assertSearchMode(window, searchMode);
  }
  Assert.equal(
    gURLBar.value,
    expectedValue,
    "The search restriction token should have been entered."
  );

  await UrlbarTestUtils.promisePopupClose(window);
}

add_setup(async function () {
  // Tests get no default Top Sites, and clearURLs opens the view on an empty
  // string, which would otherwise leave it closed with no results.
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });
});

add_task(async function insertTokens() {
  const tests = [
    {
      input: "mozilla",
      token: UrlbarShared.RESTRICT_TOKENS.HISTORY,
    },
    {
      input: "mozilla",
      token: UrlbarShared.RESTRICT_TOKENS.BOOKMARK,
    },
    {
      input: "mozilla",
      token: UrlbarShared.RESTRICT_TOKENS.TAG,
    },
    {
      input: "mozilla",
      token: UrlbarShared.RESTRICT_TOKENS.OPENPAGE,
    },
  ];
  for (let test of tests) {
    await searchAndCheckState(test);
  }
});

add_task(async function existingTokens() {
  const tests = [
    {
      input: "* mozilla",
      token: UrlbarShared.RESTRICT_TOKENS.HISTORY,
    },
    {
      input: "+ mozilla",
      token: UrlbarShared.RESTRICT_TOKENS.BOOKMARK,
    },
    {
      input: "( $ ^ mozilla",
      token: UrlbarShared.RESTRICT_TOKENS.TAG,
    },
    {
      input: "^*+%?#$ mozilla",
      token: UrlbarShared.RESTRICT_TOKENS.TAG,
    },
  ];
  for (let test of tests) {
    await searchAndCheckState(test);
  }
});

add_task(async function stripSpaces() {
  const tests = [
    {
      input: "     ^     mozilla",
      token: UrlbarShared.RESTRICT_TOKENS.HISTORY,
    },
    {
      input: "     +         mozilla   ",
      token: UrlbarShared.RESTRICT_TOKENS.BOOKMARK,
    },
    {
      input: "  moz    illa  ",
      token: UrlbarShared.RESTRICT_TOKENS.TAG,
    },
  ];
  for (let test of tests) {
    await searchAndCheckState(test);
  }
});

add_task(async function clearURLs() {
  const tests = [
    {
      loadUrl: "http://example.com/",
      token: UrlbarShared.RESTRICT_TOKENS.HISTORY,
    },
    {
      loadUrl: "about:mozilla",
      token: UrlbarShared.RESTRICT_TOKENS.BOOKMARK,
    },
  ];
  let win = BrowserWindowTracker.getTopWindow();
  await UrlbarTestUtils.promisePopupClose(win);
  for (let { loadUrl, token } of tests) {
    let browser = win.gBrowser.selectedBrowser;
    let loadedPromise = BrowserTestUtils.browserLoaded(browser, false, loadUrl);
    BrowserTestUtils.startLoadingURIString(browser, loadUrl);
    await loadedPromise;
    if (win.gURLBar.getAttribute("pageproxystate") != "valid") {
      await TestUtils.waitForCondition(
        () => win.gURLBar.getAttribute("pageproxystate") == "valid"
      );
    }
    await searchAndCheckState({ input: "", token });
  }
});
