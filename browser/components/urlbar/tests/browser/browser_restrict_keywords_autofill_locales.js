/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

// Tests autofill functionality for restrict keywords (@tabs, @bookmarks,
// @history, @actions) in localized language and in english. The autofill
// is tested by typing the full or partial keyword to enter search mode.

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  UrlbarTokenizer:
    "moz-src:///browser/components/urlbar/UrlbarTokenizer.sys.mjs",
});

let gFluentStrings = new Localization(["browser/browser.ftl"]);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.searchRestrictKeywords.featureGate", true],
      // Tests get no default Top Sites. Leaving the actions search mode runs a
      // query on an empty string, and the view only drops its actions state
      // once it has results to render, so give it one to find.
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });

  let italianEnglishKeywords = new Map([
    ["^", ["Cronologia", "History"]],
    ["*", ["Segnalibri", "Bookmarks"]],
    ["%", ["Schede", "Tabs"]],
    [">", ["Azioni", "Actions"]],
  ]);

  let tokenizerStub = sinon.stub(UrlbarTokenizer, "getL10nRestrictKeywords");
  tokenizerStub.resolves(italianEnglishKeywords);

  registerCleanupFunction(async function () {
    sinon.restore();
  });
});

async function assertAutofill(
  searchString,
  searchMode,
  entry,
  userEventAction
) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: searchString,
  });

  if (userEventAction) {
    userEventAction();
  }
  await UrlbarTestUtils.assertSearchMode(window, {
    ...searchMode,
    entry,
    restrictType: "keyword",
  });

  await UrlbarTestUtils.exitSearchMode(window);
}

async function getSearchModeKeywords() {
  let searchModeKeys = [
    "urlbar-search-mode-bookmarks",
    "urlbar-search-mode-tabs",
    "urlbar-search-mode-history",
    "urlbar-search-mode-actions",
  ];

  let [bookmarks, tabs, history, actions] = await Promise.all(
    searchModeKeys.map(key =>
      gFluentStrings.formatValue(key).then(str => `@${str.toLowerCase()}`)
    )
  );

  return [bookmarks, tabs, history, actions];
}

add_task(async function test_autofill_enters_search_mode_it_en_locales() {
  let [bookmarks, tabs, history, actions] = await getSearchModeKeywords();

  const keywordsToToken = new Map([
    [["@cronologia", history], UrlbarShared.RESTRICT_TOKENS.HISTORY],
    [["@segnalibri", bookmarks], UrlbarShared.RESTRICT_TOKENS.BOOKMARK],
    [["@schede", tabs], UrlbarShared.RESTRICT_TOKENS.OPENPAGE],
    [["@azioni", actions], UrlbarShared.RESTRICT_TOKENS.ACTION],
  ]);

  for (const [keywords, token] of keywordsToToken) {
    let searchMode = gURLBar.searchModeForToken(token);
    let italianKeyword = `${keywords[0]} `;
    let englishKeyword = `${keywords[1]} `;

    info("Test full italian keyword");
    await assertAutofill(italianKeyword, searchMode, "typed", null);

    info("Test partial italian keyword autofill by pressing right arrow");
    italianKeyword = italianKeyword.slice(0, 3);
    await assertAutofill(italianKeyword, searchMode, "typed", () =>
      EventUtils.synthesizeKey("KEY_ArrowRight")
    );

    info("Test partial italian keyword autofill by pressing enter");
    await assertAutofill(italianKeyword, searchMode, "keywordoffer", () =>
      EventUtils.synthesizeKey("KEY_Enter")
    );

    info("Test full english keyword");
    await assertAutofill(englishKeyword, searchMode, "typed", null);

    info("Test partial english keyword autofill by pressing right arrow");
    englishKeyword = englishKeyword.slice(0, 3);
    await assertAutofill(englishKeyword, searchMode, "typed", () =>
      EventUtils.synthesizeKey("KEY_ArrowRight")
    );

    info("Test partial english keyword autofill by pressing enter");
    await assertAutofill(englishKeyword, searchMode, "keywordoffer", () =>
      EventUtils.synthesizeKey("KEY_Enter")
    );
  }
});
