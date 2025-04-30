/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for mdn suggestions.

const REMOTE_SETTINGS_DATA = [
  {
    type: "mdn-suggestions",
    attachment: [
      {
        url: "https://example.com/array-filter",
        title: "Array.prototype.filter()",
        description:
          "The filter() method creates a shallow copy of a portion of a given array, filtered down to just the elements from the given array that pass the test implemented by the provided function.",
        keywords: ["array"],
        score: 0.24,
      },
    ],
  },
];

// Avoid timeouts in verify mode. They're especially common on Mac.
requestLongerTimeout(5);

add_setup(async function () {
  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: REMOTE_SETTINGS_DATA,
  });
});

add_task(async function basic() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    const suggestion = REMOTE_SETTINGS_DATA[0].attachment[0];
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: suggestion.keywords[0],
    });
    Assert.equal(UrlbarTestUtils.getResultCount(window), 2);

    const { element, result } = await UrlbarTestUtils.getDetailsOfResultAt(
      window,
      1
    );
    Assert.equal(
      result.providerName,
      UrlbarProviderQuickSuggest.name,
      "The result should be from the expected provider"
    );
    Assert.equal(result.payload.provider, "Mdn");

    const onLoad = BrowserTestUtils.browserLoaded(
      gBrowser.selectedBrowser,
      false,
      result.payload.url
    );
    EventUtils.synthesizeMouseAtCenter(element.row, {});
    await onLoad;
    Assert.ok(true, "Expected page is loaded");
  });

  await PlacesUtils.history.clear();
});

// Tests the row/group label.
add_task(async function rowLabel() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: REMOTE_SETTINGS_DATA[0].attachment[0].keywords[0],
  });
  Assert.equal(UrlbarTestUtils.getResultCount(window), 2);

  const { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
  const row = element.row;
  Assert.equal(row.getAttribute("label"), "Recommended resource");

  await UrlbarTestUtils.promisePopupClose(window);
});

add_task(async function disable() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.mdn.featureGate", false]],
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "array",
  });
  Assert.equal(UrlbarTestUtils.getResultCount(window), 1);

  const { result } = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(result.providerName, "HeuristicFallback");

  await SpecialPowers.popPrefEnv();
  await QuickSuggestTestUtils.forceSync();
});

// Tests the "Not interested" result menu dismissal command.
add_task(async function resultMenu_notInterested() {
  let result = await doDismissTest("not_interested");

  Assert.equal(UrlbarPrefs.get("suggest.mdn"), false);
  Assert.ok(
    !(await QuickSuggest.isResultDismissed(result)),
    "The result should not be dismissed"
  );

  // Re-enable suggestions and wait until MDNSuggestions syncs them from
  // remote settings again.
  UrlbarPrefs.set("suggest.mdn", true);
  await QuickSuggestTestUtils.forceSync();
});

// Tests the "Not relevant" result menu dismissal command.
add_task(async function resultMenu_notRelevant() {
  let result = await doDismissTest("not_relevant");

  Assert.equal(UrlbarPrefs.get("suggest.mdn"), true);
  Assert.ok(
    await QuickSuggest.isResultDismissed(result),
    "The result should be dismissed"
  );

  await QuickSuggest.clearDismissedSuggestions();
});

// Tests the "Manage" result menu.
add_task(async function resultMenu_manage() {
  await doManageTest({ input: "array", index: 1 });
});

async function doDismissTest(command) {
  const keyword = REMOTE_SETTINGS_DATA[0].attachment[0].keywords[0];
  // Do a search.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: keyword,
  });

  // Check the result.
  const resultCount = 2;
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    resultCount,
    "There should be two results"
  );

  const resultIndex = 1;
  let details = await UrlbarTestUtils.getDetailsOfResultAt(window, resultIndex);
  Assert.equal(
    details.result.providerName,
    UrlbarProviderQuickSuggest.name,
    "The result should be from the expected provider"
  );
  Assert.equal(
    details.result.payload.telemetryType,
    "mdn",
    "The result should be a MDN result"
  );

  let { result } = details;

  // Click the command.
  let dismissalPromise = TestUtils.topicObserved(
    "quicksuggest-dismissals-changed"
  );
  await UrlbarTestUtils.openResultMenuAndClickItem(
    window,
    ["[data-l10n-id=firefox-suggest-command-dont-show-mdn]", command],
    { resultIndex, openByMouse: true }
  );
  info("Awaiting dismissal promise");
  await dismissalPromise;

  // The row should be a tip now.
  Assert.ok(gURLBar.view.isOpen, "The view should remain open after dismissal");
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    resultCount,
    "The result count should not haved changed after dismissal"
  );
  details = await UrlbarTestUtils.getDetailsOfResultAt(window, resultIndex);
  Assert.equal(
    details.type,
    UrlbarUtils.RESULT_TYPE.TIP,
    "Row should be a tip after dismissal"
  );
  Assert.equal(
    details.result.payload.type,
    "dismissalAcknowledgment",
    "Tip type should be dismissalAcknowledgment"
  );
  Assert.ok(
    !details.element.row.hasAttribute("feedback-acknowledgment"),
    "Row should not have feedback acknowledgment after dismissal"
  );

  // Get the dismissal acknowledgment's "Got it" button and click it.
  const gotItButton = UrlbarTestUtils.getButtonForResultIndex(
    window,
    0,
    resultIndex
  );
  Assert.ok(gotItButton, "Row should have a 'Got it' button");
  EventUtils.synthesizeMouseAtCenter(gotItButton, {}, window);

  // The view should remain open and the tip row should be gone.
  Assert.ok(
    gURLBar.view.isOpen,
    "The view should remain open clicking the 'Got it' button"
  );
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    resultCount - 1,
    "The result count should be one less after clicking 'Got it' button"
  );
  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    Assert.ok(
      details.type != UrlbarUtils.RESULT_TYPE.TIP &&
        details.result.payload.telemetryType !== "mdn",
      "Tip result and suggestion should not be present"
    );
  }

  gURLBar.handleRevert();

  // Do the search again.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: keyword,
  });
  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    Assert.ok(
      details.type != UrlbarUtils.RESULT_TYPE.TIP &&
        details.result.payload.telemetryType !== "mdn",
      "Tip result and suggestion should not be present"
    );
  }

  await UrlbarTestUtils.promisePopupClose(window);

  return result;
}
