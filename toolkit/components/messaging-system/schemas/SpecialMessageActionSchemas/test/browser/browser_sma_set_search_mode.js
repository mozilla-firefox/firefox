/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Verifies that the SET_SEARCH_MODE action correctly enters search mode
 * with the expected engine and properties.
 */

"use strict";

ChromeUtils.defineLazyGetter(this, "UrlbarTestUtils", () => {
  const { UrlbarTestUtils } = ChromeUtils.importESModule(
    "resource://testing-common/UrlbarTestUtils.sys.mjs"
  );
  UrlbarTestUtils.init(this);
  return UrlbarTestUtils;
});

add_setup(async function () {
  // Tests get no default Top Sites. Leaving search mode here does so with an
  // empty input, and without a result the view never opens for exitSearchMode
  // to wait on.
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });
});

add_task(async function test_SET_SEARCH_MODE() {
  let searchMode = {
    engineName: "Bing",
    isGeneralPurposeEngine: true,
    source: 3,
    isPreview: false,
    entry: "messagingSystem",
  };

  let action = {
    type: "SET_SEARCH_MODE",
    data: searchMode,
    dismiss: true,
  };

  await SMATestUtils.executeAndValidateAction(action);
  await UrlbarTestUtils.assertSearchMode(window, searchMode);
  await UrlbarTestUtils.exitSearchMode(window);
});
