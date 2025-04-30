/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests browser quick suggestions.
 */

const REMOTE_SETTINGS_RESULTS = [
  QuickSuggestTestUtils.ampRemoteSettings({
    keywords: ["fra", "frab"],
    full_keywords: [["frab", 2]],
  }),
  QuickSuggestTestUtils.wikipediaRemoteSettings(),
];

const MERINO_NAVIGATIONAL_SUGGESTION = {
  url: "https://example.com/navigational-suggestion",
  title: "Navigational suggestion",
  provider: "top_picks",
  is_sponsored: false,
  score: 0.25,
  block_id: 0,
  is_top_pick: true,
};

const MERINO_DYNAMIC_WIKIPEDIA_SUGGESTION = {
  url: "https://example.com/dynamic-wikipedia",
  title: "Dynamic Wikipedia suggestion",
  click_url: "https://example.com/click",
  impression_url: "https://example.com/impression",
  advertiser: "dynamic-wikipedia",
  provider: "wikipedia",
  iab_category: "5 - Education",
  block_id: 1,
};

// Trying to avoid timeouts in TV mode.
requestLongerTimeout(5);

add_setup(async function () {
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
  await UrlbarTestUtils.formHistory.clear();

  let isAmp = suggestion => suggestion.iab_category == "22 - Shopping";
  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: [
      {
        collection: QuickSuggestTestUtils.RS_COLLECTION.AMP,
        type: QuickSuggestTestUtils.RS_TYPE.AMP,
        attachment: REMOTE_SETTINGS_RESULTS.filter(isAmp),
      },
      {
        collection: QuickSuggestTestUtils.RS_COLLECTION.OTHER,
        type: QuickSuggestTestUtils.RS_TYPE.WIKIPEDIA,
        attachment: REMOTE_SETTINGS_RESULTS.filter(s => !isAmp(s)),
      },
    ],
    merinoSuggestions: [],
  });

  // Disable Merino so we trigger only remote settings suggestions.
  UrlbarPrefs.set("quicksuggest.dataCollection.enabled", false);

  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.suggest.engines", false]],
  });
});

// Tests a sponsored result and keyword highlighting.
add_task(async function sponsored() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "fra",
  });
  await QuickSuggestTestUtils.assertIsQuickSuggest({
    window,
    index: 1,
    isSponsored: true,
    url: "https://example.com/amp",
  });
  let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
  Assert.equal(
    row.querySelector(".urlbarView-title").firstChild.textContent,
    "fra",
    "The part of the keyword that matches users input is not bold."
  );
  Assert.equal(
    row.querySelector(".urlbarView-title > strong").textContent,
    "b",
    "The auto completed section of the keyword is bolded."
  );
  await UrlbarTestUtils.promisePopupClose(window);
});

// Tests a non-sponsored result.
add_task(async function nonSponsored() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "wikipedia",
  });
  await QuickSuggestTestUtils.assertIsQuickSuggest({
    window,
    index: 1,
    isSponsored: false,
    url: "https://example.com/wikipedia",
  });
  await UrlbarTestUtils.promisePopupClose(window);
});

// Tests sponsored priority feature.
add_task(async function sponsoredPriority() {
  const cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
    quickSuggestSponsoredPriority: true,
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "fra",
  });
  await QuickSuggestTestUtils.assertIsQuickSuggest({
    window,
    index: 1,
    isSponsored: true,
    isBestMatch: true,
    url: "https://example.com/amp",
  });

  let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
  Assert.equal(
    row.querySelector(".urlbarView-title").firstChild.textContent,
    "fra",
    "The part of the keyword that matches users input is not bold."
  );
  Assert.equal(
    row.querySelector(".urlbarView-title > strong").textContent,
    "b",
    "The auto completed section of the keyword is bolded."
  );

  // Group label.
  let before = window.getComputedStyle(row, "::before");
  Assert.equal(before.content, "attr(label)", "::before.content is enabled");
  Assert.equal(
    row.getAttribute("label"),
    "Top pick",
    "Row has 'Top pick' group label"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  await cleanUpNimbus();
});

// Tests sponsored priority feature does not affect to non-sponsored suggestion.
add_task(async function sponsoredPriorityButNotSponsoredSuggestion() {
  const cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
    quickSuggestSponsoredPriority: true,
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "wikipedia",
  });
  await QuickSuggestTestUtils.assertIsQuickSuggest({
    window,
    index: 1,
    isSponsored: false,
    url: "https://example.com/wikipedia",
  });

  let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
  let before = window.getComputedStyle(row, "::before");
  Assert.equal(before.content, "attr(label)", "::before.content is enabled");
  Assert.equal(
    row.getAttribute("label"),
    "Firefox Suggest",
    "Row has general group label for quick suggest"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  await cleanUpNimbus();
});

// AMP should be a top pick when quickSuggestAmpTopPickCharThreshold is non-zero
// and the matched keyword/search string meets the threshold.
add_task(async function ampTopPickCharThreshold_meetsThreshold() {
  // Search with a non-full keyword just to make sure that doesn't prevent the
  // suggestion from being a top pick. "fra" is the query, "frab" is the full
  // keyword.
  let query = "fra";
  const cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
    quickSuggestAmpTopPickCharThreshold: query.length,
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: query,
  });
  await QuickSuggestTestUtils.assertIsQuickSuggest({
    window,
    index: 1,
    isSponsored: true,
    isBestMatch: true,
    hasSponsoredLabel: false,
    url: "https://example.com/amp",
  });

  let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
  Assert.equal(
    row.querySelector(".urlbarView-title > strong").textContent,
    query,
    "The title should include the full keyword and the part that matches the query should be bold"
  );

  // Group label.
  let before = window.getComputedStyle(row, "::before");
  Assert.equal(before.content, "attr(label)", "::before.content is enabled");
  Assert.equal(
    row.getAttribute("label"),
    "Sponsored",
    "Row has 'Sponsored' group label"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  await cleanUpNimbus();
});

// AMP should not be a top pick when quickSuggestAmpTopPickCharThreshold is
// non-zero and a typed non-full keyword falls below the threshold.
add_task(async function ampTopPickCharThreshold_belowThreshold() {
  // Search with a full keyword just to make sure that doesn't cause the
  // suggestion to be a top pick.
  let queryAndFullKeyword = "frab";
  const cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
    quickSuggestAmpTopPickCharThreshold: 100,
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: queryAndFullKeyword,
  });
  await QuickSuggestTestUtils.assertIsQuickSuggest({
    window,
    index: 1,
    isSponsored: true,
    url: "https://example.com/amp",
  });

  let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
  Assert.ok(
    !row.querySelector(".urlbarView-title > strong"),
    "Since the full keyword was matched, the title shouldn't have any bold text"
  );

  // Group label.
  let before = window.getComputedStyle(row, "::before");
  Assert.equal(before.content, "attr(label)", "::before.content is enabled");
  Assert.equal(
    row.getAttribute("label"),
    "Firefox Suggest",
    "Row has 'Firefox Suggest' group label"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  await cleanUpNimbus();
});

// Tests the "Manage" result menu for sponsored suggestion.
add_task(async function resultMenu_manage_sponsored() {
  await doManageTest({
    input: "fra",
    index: 1,
  });
});

// Tests the "Manage" result menu for non-sponsored suggestion.
add_task(async function resultMenu_manage_nonSponsored() {
  await doManageTest({
    input: "wikipedia",
    index: 1,
  });
});

// Tests the "Manage" result menu for Navigational suggestion.
add_task(async function resultMenu_manage_navigational() {
  // Enable Merino.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.quicksuggest.dataCollection.enabled", true]],
  });

  MerinoTestUtils.server.response.body.suggestions = [
    MERINO_NAVIGATIONAL_SUGGESTION,
  ];

  await doManageTest({
    input: "test",
    index: 1,
  });

  await SpecialPowers.popPrefEnv();
});

// Tests the "Manage" result menu for Dynamic Wikipedia suggestion.
add_task(async function resultMenu_manage_dynamicWikipedia() {
  // Enable Merino.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.quicksuggest.dataCollection.enabled", true]],
  });
  MerinoTestUtils.server.response.body.suggestions = [
    MERINO_DYNAMIC_WIKIPEDIA_SUGGESTION,
  ];

  await doManageTest({
    input: "test",
    index: 1,
  });

  await SpecialPowers.popPrefEnv();
});
