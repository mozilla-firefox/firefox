/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.ipc.chromeMessagePassing", false],
      // Tests get no default Top Sites, but this test opens the view on an
      // empty string and expects a result even when the opt-in is hidden.
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });

  registerCleanupFunction(async () => {
    UrlbarPrefs.clear("quicksuggest.online.enabled");
    UrlbarPrefs.clear("quicksuggest.contextualOptIn");
    UrlbarPrefs.clear("quicksuggest.contextualOptIn.lastDismissedTime");
    UrlbarPrefs.clear("quicksuggest.contextualOptIn.dismissedCount");
    UrlbarPrefs.clear("quicksuggest.contextualOptIn.firstImpressionTime");
    UrlbarPrefs.clear("quicksuggest.contextualOptIn.impressionCount");
    UrlbarPrefs.clear(
      "quicksuggest.contextualOptIn.firstReshowAfterPeriodDays"
    );
    UrlbarPrefs.clear(
      "quicksuggest.contextualOptIn.secondReshowAfterPeriodDays"
    );
    UrlbarPrefs.clear(
      "quicksuggest.contextualOptIn.thirdReshowAfterPeriodDays"
    );
    UrlbarPrefs.clear("quicksuggest.contextualOptIn.impressionLimit");
    UrlbarPrefs.clear("quicksuggest.contextualOptIn.impressionDaysLimit");
  });
});

add_task(async function accept() {
  info("Setup");
  UrlbarPrefs.set("quicksuggest.online.enabled", false);
  UrlbarPrefs.set("quicksuggest.contextualOptIn", true);
  UrlbarPrefs.set("quicksuggest.contextualOptIn.dismissedCount", 0);
  UrlbarPrefs.set("quicksuggest.contextualOptIn.lastDismissedTime", 0);

  info("Open urlbar results");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });

  info("Check the contextual opt-in result");
  let { element, result } = await UrlbarTestUtils.getDetailsOfResultAt(
    window,
    0
  );
  Assert.equal(
    result.providerName,
    "UrlbarProviderQuickSuggestContextualOptIn"
  );

  info("Accept the contextual opt-in");
  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeMouseAtCenter(
      element.row.querySelector("[name=allow]"),
      {},
      window
    );
  });
  Assert.ok(UrlbarPrefs.get("quicksuggest.online.enabled"));

  info(
    "Check whether the contextual opt-in result was removed from last query"
  );
  let { queryContext } = gURLBar.controller._lastQueryContextWrapper;
  Assert.ok(
    !queryContext.results.some(
      r => r.providerName == "UrlbarProviderQuickSuggestContextualOptIn"
    )
  );
});

add_task(async function dismiss() {
  info("Setup");
  UrlbarPrefs.set("quicksuggest.online.enabled", false);
  UrlbarPrefs.set("quicksuggest.contextualOptIn", true);
  UrlbarPrefs.set("quicksuggest.contextualOptIn.lastDismissedTime", 0);
  UrlbarPrefs.set("quicksuggest.contextualOptIn.dismissedCount", 0);
  UrlbarPrefs.set("quicksuggest.contextualOptIn.firstReshowAfterPeriodDays", 2);
  UrlbarPrefs.set(
    "quicksuggest.contextualOptIn.secondReshowAfterPeriodDays",
    3
  );
  UrlbarPrefs.set("quicksuggest.contextualOptIn.impressionCount", 0);
  UrlbarPrefs.clear("quicksuggest.contextualOptIn.impressionLimit");
  UrlbarPrefs.set("quicksuggest.contextualOptIn.thirdReshowAfterPeriodDays", 4);

  info("First dismissal");
  await assertContextualOptinVisibility({ visible: true });
  await doDismiss();
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.dismissedCount"),
    1
  );
  await assertContextualOptinVisibility({ visible: false });

  info(
    "Move lastDismissedTime date 1 day earlier than current lastDismissedTime"
  );
  let lastDismissedTime = UrlbarPrefs.get(
    "quicksuggest.contextualOptIn.lastDismissedTime"
  );
  moveLastDismissedTimeEalier(lastDismissedTime, 1);
  await assertContextualOptinVisibility({ visible: false });

  info("Move lastDismissedTime date 2 days earlier");
  moveLastDismissedTimeEalier(lastDismissedTime, 2);
  await assertContextualOptinVisibility({ visible: true });

  info("Second dismissal");
  await doDismiss();
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.dismissedCount"),
    2
  );
  await assertContextualOptinVisibility({ visible: false });

  info("Move lastDismissedTime date 2 days earlier");
  lastDismissedTime = UrlbarPrefs.get(
    "quicksuggest.contextualOptIn.lastDismissedTime"
  );
  moveLastDismissedTimeEalier(lastDismissedTime, 2);
  await assertContextualOptinVisibility({ visible: false });

  info("Move lastDismissedTime date 3 days earlier");
  moveLastDismissedTimeEalier(lastDismissedTime, 3);
  await assertContextualOptinVisibility({ visible: true });

  info("Third dismissal");
  await doDismiss();
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.dismissedCount"),
    3
  );
  await assertContextualOptinVisibility({ visible: false });

  info("Move lastDismissedTime date 3 days earlier");
  lastDismissedTime = UrlbarPrefs.get(
    "quicksuggest.contextualOptIn.lastDismissedTime"
  );
  moveLastDismissedTimeEalier(lastDismissedTime, 3);
  await assertContextualOptinVisibility({ visible: false });

  info("Move lastDismissedTime date 4 days earlier");
  moveLastDismissedTimeEalier(lastDismissedTime, 4);
  await assertContextualOptinVisibility({ visible: true });

  info("Fourth dismissal");
  await doDismiss();
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.dismissedCount"),
    4
  );
  await assertContextualOptinVisibility({ visible: false });

  info("Move lastDismissedTime date 360 days earlier");
  lastDismissedTime = UrlbarPrefs.get(
    "quicksuggest.contextualOptIn.lastDismissedTime"
  );
  moveLastDismissedTimeEalier(lastDismissedTime, 360);
  await assertContextualOptinVisibility({ visible: false });
});

add_task(async function dismiss_by_impressions_count_fisrt() {
  info("Setup");
  const IMPRESSION_LIMIT = 5;
  const IMPRESSION_DAYS_LIMIT = 10;
  UrlbarPrefs.set("quicksuggest.online.enabled", false);
  UrlbarPrefs.set("quicksuggest.contextualOptIn", true);
  UrlbarPrefs.set("quicksuggest.contextualOptIn.dismissedCount", 0);
  UrlbarPrefs.set("quicksuggest.contextualOptIn.lastDismissedTime", 0);
  UrlbarPrefs.set("quicksuggest.contextualOptIn.impressionCount", 0);
  UrlbarPrefs.set(
    "quicksuggest.contextualOptIn.impressionLimit",
    IMPRESSION_LIMIT
  );
  UrlbarPrefs.set("quicksuggest.contextualOptIn.firstImpressionTime", 0);
  UrlbarPrefs.set(
    "quicksuggest.contextualOptIn.impressionDaysLimit",
    IMPRESSION_DAYS_LIMIT
  );

  info("Make impressions more than limit");
  for (let i = 0; i < IMPRESSION_LIMIT + 1; i++) {
    await makeImpression();
  }
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.impressionCount"),
    IMPRESSION_LIMIT + 1
  );

  info("Contextual opt-in result should show since no day passed");
  await assertContextualOptinVisibility({ visible: true });

  info("Simulate IMPRESSION_DAYS_LIMIT - 1 days later");
  moveFirstImpressionTimeEalier(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.firstImpressionTime"),
    IMPRESSION_DAYS_LIMIT - 1
  );
  await assertContextualOptinVisibility({ visible: true });
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.dismissedCount"),
    0,
    "Still not dismissed"
  );

  info("Simulate 1 more day later (total IMPRESSION_DAYS_LIMIT days)");
  moveFirstImpressionTimeEalier(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.firstImpressionTime"),
    1
  );
  // `assertContextualOptinVisibility` shows the urlbar result. As this calls
  // `isActive()` internally, the impressions will be evaluate and dismiss if needed.
  await assertContextualOptinVisibility({ visible: false });
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.dismissedCount"),
    1,
    "Dismissed"
  );
});

add_task(async function dismiss_by_impressions_elapsed_days_fisrt() {
  info("Setup");
  const IMPRESSION_LIMIT = 5;
  const IMPRESSION_DAYS_LIMIT = 10;
  UrlbarPrefs.set("quicksuggest.online.enabled", false);
  UrlbarPrefs.set("quicksuggest.contextualOptIn", true);
  UrlbarPrefs.set("quicksuggest.contextualOptIn.dismissedCount", 0);
  UrlbarPrefs.set("quicksuggest.contextualOptIn.lastDismissedTime", 0);
  UrlbarPrefs.set("quicksuggest.contextualOptIn.impressionCount", 0);
  UrlbarPrefs.set(
    "quicksuggest.contextualOptIn.impressionLimit",
    IMPRESSION_LIMIT
  );
  UrlbarPrefs.set("quicksuggest.contextualOptIn.firstImpressionTime", 0);
  UrlbarPrefs.set(
    "quicksuggest.contextualOptIn.impressionDaysLimit",
    IMPRESSION_DAYS_LIMIT
  );

  info("Make first impression");
  await makeImpression();

  info("Simulate more than impressionDaysLimit days later");
  moveFirstImpressionTimeEalier(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.firstImpressionTime"),
    IMPRESSION_DAYS_LIMIT + 1
  );
  await assertContextualOptinVisibility({ visible: true });
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.dismissedCount"),
    0,
    "Still not dismissed"
  );

  info("Make impressions to the limit - 1");
  for (let i = 0; i < IMPRESSION_LIMIT - 2; i++) {
    await makeImpression();
  }
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.impressionCount"),
    IMPRESSION_LIMIT - 1
  );
  await assertContextualOptinVisibility({ visible: true });
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.dismissedCount"),
    0,
    "Still not dismissed"
  );

  info("Make impressions to the limit");
  await makeImpression();
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.impressionCount"),
    IMPRESSION_LIMIT
  );
  await assertContextualOptinVisibility({ visible: false });
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.dismissedCount"),
    1,
    "Dismissed"
  );
});

async function doDismiss() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });

  let { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);

  info("Dismiss the contextual opt-in");
  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeMouseAtCenter(
      element.row.querySelector("[name=dismiss]"),
      {},
      window
    );
  });
}

async function assertContextualOptinVisibility({ visible }) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });

  let { result } = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);

  if (visible) {
    Assert.equal(
      result.providerName,
      "UrlbarProviderQuickSuggestContextualOptIn",
      "Contextual opt-in result shoud be shown"
    );
  } else {
    Assert.notEqual(
      result.providerName,
      "UrlbarProviderQuickSuggestContextualOptIn",
      "Contextual opt-in result shoud not be shown"
    );
  }

  await UrlbarTestUtils.promisePopupClose(window);
}

function moveLastDismissedTimeEalier(lastDismissedTime, days) {
  UrlbarPrefs.set(
    "quicksuggest.contextualOptIn.lastDismissedTime",
    subtractDays(lastDismissedTime, days)
  );
}

function moveFirstImpressionTimeEalier(firstImpressionTime, days) {
  UrlbarPrefs.set(
    "quicksuggest.contextualOptIn.firstImpressionTime",
    subtractDays(firstImpressionTime, days)
  );
}

function subtractDays(timeInSeconds, days) {
  // Subtract an extra 60s just to make sure the returned time is at least
  // `days` ago.
  return timeInSeconds - days * 24 * 60 * 60 - 60;
}

async function makeImpression() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });
  let { result } = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(
    result.providerName,
    "UrlbarProviderQuickSuggestContextualOptIn"
  );
  await UrlbarTestUtils.promisePopupClose(window, () => gURLBar.blur());
}

add_task(async function nimbus() {
  UrlbarPrefs.clear("quicksuggest.contextualOptIn");
  UrlbarPrefs.clear("quicksuggest.contextualOptIn.firstReshowAfterPeriodDays");
  UrlbarPrefs.clear("quicksuggest.contextualOptIn.secondReshowAfterPeriodDays");
  UrlbarPrefs.clear("quicksuggest.contextualOptIn.thirdReshowAfterPeriodDays");
  UrlbarPrefs.clear("quicksuggest.contextualOptIn.impressionLimit");
  UrlbarPrefs.clear("quicksuggest.contextualOptIn.impressionDaysLimit");

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.quicksuggest.contextualOptIn", false],
      [
        "browser.urlbar.quicksuggest.contextualOptIn.firstReshowAfterPeriodDays",
        7,
      ],
      [
        "browser.urlbar.quicksuggest.contextualOptIn.secondReshowAfterPeriodDays",
        14,
      ],
      [
        "browser.urlbar.quicksuggest.contextualOptIn.thirdReshowAfterPeriodDays",
        60,
      ],
      ["browser.urlbar.quicksuggest.contextualOptIn.impressionLimit", 20],
      ["browser.urlbar.quicksuggest.contextualOptIn.impressionDaysLimit", 5],
    ],
  });

  let cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
    quickSuggestContextualOptInEnabled: true,
    quickSuggestContextualOptInFirstReshowAfterPeriodDays: 100,
    quickSuggestContextualOptInSecondReshowAfterPeriodDays: 200,
    quickSuggestContextualOptInThirdReshowAfterPeriodDays: 300,
    quickSuggestContextualOptInImpressionLimit: 400,
    quickSuggestContextualOptInImpressionDaysLimit: 500,
  });

  Assert.equal(UrlbarPrefs.get("quicksuggest.contextualOptIn"), true);
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.firstReshowAfterPeriodDays"),
    100
  );
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.secondReshowAfterPeriodDays"),
    200
  );
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.thirdReshowAfterPeriodDays"),
    300
  );
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.impressionLimit"),
    400
  );
  Assert.equal(
    UrlbarPrefs.get("quicksuggest.contextualOptIn.impressionDaysLimit"),
    500
  );

  await cleanUpNimbus();
  await SpecialPowers.popPrefEnv();
});
