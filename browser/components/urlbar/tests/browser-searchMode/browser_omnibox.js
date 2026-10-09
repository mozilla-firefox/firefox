/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests omnibox urlbar results with search mode.
 */

const ENGINE_NAME = "Example";
add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.scotchBonnet.enableOverride", false],
      // Tests get no default Top Sites, and this test opens the view on an
      // empty string, which would otherwise leave it closed with no results.
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });
  await SearchTestUtils.installSearchExtension({
    name: ENGINE_NAME,
  });
});

add_task(async function test_omnibox_searchMode() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      omnibox: {
        keyword: "omnibox",
      },
    },
    background() {
      /* global browser */
      browser.omnibox.setDefaultSuggestion({
        description: "default suggestion",
      });
    },
  });

  await extension.startup();

  info("Focus urlbar");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });

  info("Enter search mode");
  await UrlbarTestUtils.enterSearchMode(window, {
    engineName: ENGINE_NAME,
  });
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: ENGINE_NAME,
    entry: "oneoff",
  });

  info("Enter search term with omnibox keyword");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "omnibox 123",
    fireInputEvent: true,
  });

  assertNoOmniboxResult(window);

  await extension.unload();
  await UrlbarTestUtils.exitSearchMode(window);
});

add_task(async function test_omnibox_searchMode_switch() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      omnibox: {
        keyword: "omnibox",
      },
    },
    background() {
      browser.omnibox.setDefaultSuggestion({
        description: "default suggestion",
      });
    },
  });

  await extension.startup();

  info("Enter search term with omnibox keyword");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "omnibox 123",
    fireInputEvent: true,
  });

  let { result } = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);

  Assert.equal(
    result.providerName,
    "UrlbarProviderOmnibox",
    "First result should be default omnibox suggestion."
  );

  info("Enter search mode");
  await UrlbarTestUtils.enterSearchMode(window, {
    engineName: ENGINE_NAME,
  });
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: ENGINE_NAME,
    entry: "oneoff",
  });

  assertNoOmniboxResult(window);

  await extension.unload();
  await UrlbarTestUtils.exitSearchMode(window);
});

async function assertNoOmniboxResult(win) {
  let count = await UrlbarTestUtils.getResultCount(win);
  Assert.greaterOrEqual(count, 1, "There should be at least one result");
  for (let i = 0; i < count; ++i) {
    let result = await UrlbarTestUtils.getDetailsOfResultAt(win, i);
    Assert.notEqual(
      result.type,
      UrlbarUtils.RESULT_TYPE.OMNIBOX,
      "Result is not Omnibox"
    );
  }
}
