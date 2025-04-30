/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests quick suggest dismissals ("blocks").

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AmpSuggestions: "resource:///modules/urlbar/private/AmpSuggestions.sys.mjs",
});

const { TIMESTAMP_TEMPLATE } = AmpSuggestions;

// Include the timestamp template in the suggestion URLs so we can make sure
// their original URLs with the unreplaced templates are blocked and not their
// URLs with timestamps.
const REMOTE_SETTINGS_RESULTS = [
  {
    id: 1,
    url: `https://example.com/sponsored?t=${TIMESTAMP_TEMPLATE}`,
    title: "Sponsored suggestion",
    keywords: ["sponsored"],
    click_url: "https://example.com/click",
    impression_url: "https://example.com/impression",
    advertiser: "TestAdvertiser",
    iab_category: "22 - Shopping",
    icon: "1234",
  },
  {
    id: 2,
    url: `https://example.com/nonsponsored?t=${TIMESTAMP_TEMPLATE}`,
    title: "Non-sponsored suggestion",
    keywords: ["nonsponsored"],
    click_url: "https://example.com/click",
    impression_url: "https://example.com/impression",
    advertiser: "Wikipedia",
    iab_category: "5 - Education",
    icon: "1234",
  },
];

// Trying to avoid timeouts.
requestLongerTimeout(3);

add_setup(async function () {
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
  await UrlbarTestUtils.formHistory.clear();
  await QuickSuggest.clearDismissedSuggestions();

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
    prefs: [["quicksuggest.ampTopPickCharThreshold", 0]],
  });
});

// Picks the dismiss command in the result menu.
add_task(async function basic() {
  await doBasicBlockTest({
    block: async () => {
      await UrlbarTestUtils.openResultMenuAndPressAccesskey(window, "D", {
        resultIndex: 1,
      });
    },
  });
});

// Uses the key shortcut to block a suggestion.
add_task(async function basic_keyShortcut() {
  await doBasicBlockTest({
    block: () => {
      // Arrow down once to select the row.
      EventUtils.synthesizeKey("KEY_ArrowDown");
      EventUtils.synthesizeKey("KEY_Delete", { shiftKey: true });
    },
  });
});

async function doBasicBlockTest({ block }) {
  for (let result of REMOTE_SETTINGS_RESULTS) {
    info("Doing basic block test with result: " + JSON.stringify({ result }));
    await doOneBasicBlockTest({ result, block });
  }
}

async function doOneBasicBlockTest({ result, block }) {
  let isSponsored = result.iab_category != "5 - Education";

  // Do a search that triggers the suggestion.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: result.keywords[0],
  });
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    2,
    "Two rows are present after searching (heuristic + suggestion)"
  );

  let { result: urlbarResult } =
    await QuickSuggestTestUtils.assertIsQuickSuggest({
      window,
      isSponsored,
      url: isSponsored ? undefined : result.url,
      originalUrl: isSponsored ? result.url : undefined,
    });

  // Block the suggestion.
  let dismissalPromise = TestUtils.topicObserved(
    "quicksuggest-dismissals-changed"
  );
  await block();
  info("Awaiting dismissal promise");
  await dismissalPromise;

  // The row should have been removed.
  Assert.ok(
    UrlbarTestUtils.isPopupOpen(window),
    "View remains open after blocking result"
  );
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    1,
    "Only one row after blocking suggestion"
  );
  await QuickSuggestTestUtils.assertNoQuickSuggestResults(window);

  // The URL should be blocked.
  Assert.ok(
    await QuickSuggest.isResultDismissed(urlbarResult),
    "Result should be dismissed"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  await QuickSuggest.clearDismissedSuggestions();
}

// Blocks multiple suggestions one after the other.
add_task(async function blockMultiple() {
  for (let i = 0; i < REMOTE_SETTINGS_RESULTS.length; i++) {
    // Do a search that triggers the i'th suggestion.
    let { keywords, url, iab_category } = REMOTE_SETTINGS_RESULTS[i];
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: keywords[0],
    });

    let isSponsored = iab_category != "5 - Education";
    let { result: urlbarResult } =
      await QuickSuggestTestUtils.assertIsQuickSuggest({
        window,
        isSponsored,
        url: isSponsored ? undefined : url,
        originalUrl: isSponsored ? url : undefined,
      });

    // Block it.
    let dismissalPromise = TestUtils.topicObserved(
      "quicksuggest-dismissals-changed"
    );
    await UrlbarTestUtils.openResultMenuAndPressAccesskey(window, "D", {
      resultIndex: 1,
    });
    info("Awaiting dismissal promise");
    await dismissalPromise;

    Assert.ok(
      await QuickSuggest.isResultDismissed(urlbarResult),
      "Result should be dismissed after dismissing it from the menu"
    );

    // Make sure all previous suggestions remain blocked and no other
    // suggestions are blocked yet.
    for (let j = 0; j < REMOTE_SETTINGS_RESULTS.length; j++) {
      Assert.equal(
        await QuickSuggest.rustBackend.isDismissedByKey(
          REMOTE_SETTINGS_RESULTS[j].url
        ),
        j <= i,
        `Suggestion at index ${j} is blocked or not as expected`
      );
    }
  }

  await UrlbarTestUtils.promisePopupClose(window);
  await QuickSuggest.clearDismissedSuggestions();
});
