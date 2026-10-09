/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

// This test checks that searching for "@" provides restrict keywords
// (@bookmarks, @history, @tabs, @actions) and verifies that selecting one of
// these keywords enters the appropriate search mode.

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  UrlbarTokenizer:
    "moz-src:///browser/components/urlbar/UrlbarTokenizer.sys.mjs",
});

// Restrict tokens with corresponding @keywords.
const RESTRICT_TOKENS_TO_TEST = [
  UrlbarShared.RESTRICT_TOKENS.HISTORY,
  UrlbarShared.RESTRICT_TOKENS.BOOKMARK,
  UrlbarShared.RESTRICT_TOKENS.OPENPAGE,
  UrlbarShared.RESTRICT_TOKENS.ACTION,
];

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

  registerCleanupFunction(async function () {
    sinon.restore();
  });
});

async function getRestrictKeywordResult(window, restrictToken) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "@",
  });

  let restrictResult;
  let resultIndex;
  let resultCount = await UrlbarTestUtils.getResultCount(window);

  for (let index = 0; !restrictResult && index < resultCount; index++) {
    let details = await UrlbarTestUtils.getDetailsOfResultAt(window, index);
    if (details.result.type != UrlbarUtils.RESULT_TYPE.RESTRICT) {
      continue;
    }
    let l10nRestrictKeywords = details.result.payload.l10nRestrictKeywords;
    let localSearchMode = l10nRestrictKeywords[0].toLowerCase();
    let keywords = l10nRestrictKeywords
      .map(keyword => `@${keyword.toLowerCase()}`)
      .join(", ");
    let symbol = details.result.payload.keyword;

    if (symbol == restrictToken) {
      Assert.equal(
        details.displayed.title,
        `${keywords} - Search ${localSearchMode}`,
        "The result's title is set correctly."
      );

      restrictResult = await UrlbarTestUtils.waitForAutocompleteResultAt(
        window,
        index
      );
      resultIndex = index;
    }
  }

  return { restrictResult, resultIndex };
}

async function exitSearchModeAndClosePanel() {
  await UrlbarTestUtils.exitSearchMode(window);
  await UrlbarTestUtils.promisePopupClose(window, () =>
    EventUtils.synthesizeKey("KEY_Escape")
  );
}

async function assertRestrictKeywordResult(window, restrictToken) {
  let { restrictResult } = await getRestrictKeywordResult(
    window,
    restrictToken
  );

  let searchPromise = UrlbarTestUtils.promiseSearchComplete(window);
  EventUtils.synthesizeMouseAtCenter(restrictResult, {});
  await searchPromise;

  let searchMode = window.gURLBar.searchModeForToken(
    restrictResult.result.payload.keyword
  );
  await UrlbarTestUtils.assertSearchMode(window, {
    ...searchMode,
    entry: "keywordoffer",
    restrictType: "keyword",
  });

  await exitSearchModeAndClosePanel();
}

add_task(async function test_search_restrict_keyword_results() {
  for (const restrictToken of RESTRICT_TOKENS_TO_TEST) {
    await assertRestrictKeywordResult(window, restrictToken);
  }
});

add_task(async function test_search_restrict_keyword_results_es_en_locales() {
  let spanishEnglishKeywords = new Map([
    ["*", ["Marcadores", "Bookmarks"]],
    ["%", ["Pesta\xF1as", "Tabs"]],
    ["^", ["Historial", "History"]],
    [">", ["Acciones", "Actions"]],
  ]);

  let tokenizerStub = sinon.stub(UrlbarTokenizer, "getL10nRestrictKeywords");
  tokenizerStub.resolves(spanishEnglishKeywords);

  for (const restrictToken of RESTRICT_TOKENS_TO_TEST) {
    await assertRestrictKeywordResult(window, restrictToken);
  }
});
