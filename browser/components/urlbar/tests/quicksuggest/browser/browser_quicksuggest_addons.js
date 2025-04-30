/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for addon suggestions.

// The expected index of the addon suggestion.
const EXPECTED_RESULT_INDEX = 1;

// Allow more time for TV runs.
requestLongerTimeout(5);

// TODO: Firefox no longer uses `rating` and `number_of_ratings` but they are
// still present in Merino and RS suggestions, so they are included here for
// greater accuracy. We should remove them from Merino, RS, and tests.
const TEST_MERINO_SUGGESTIONS = [
  {
    provider: "amo",
    icon: "https://example.com/first.svg",
    url: "https://example.com/first-addon",
    title: "First Addon",
    description: "This is a first addon",
    custom_details: {
      amo: {
        rating: "5",
        number_of_ratings: "1234567",
        guid: "first@addon",
      },
    },
  },
  {
    provider: "amo",
    icon: "https://example.com/second.png",
    url: "https://example.com/second-addon",
    title: "Second Addon",
    description: "This is a second addon",
    custom_details: {
      amo: {
        rating: "4.5",
        number_of_ratings: "123",
        guid: "second@addon",
      },
    },
  },
  {
    provider: "amo",
    icon: "https://example.com/third.svg",
    url: "https://example.com/third-addon",
    title: "Third Addon",
    description: "This is a third addon",
    custom_details: {
      amo: {
        rating: "0",
        number_of_ratings: "0",
        guid: "third@addon",
      },
    },
  },
  {
    provider: "amo",
    icon: "https://example.com/fourth.svg",
    url: "https://example.com/fourth-addon",
    title: "Fourth Addon",
    description: "This is a fourth addon",
    custom_details: {
      amo: {
        rating: "4",
        number_of_ratings: "4",
        guid: "fourth@addon",
      },
    },
  },
];

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      // Disable search suggestions so we don't hit the network.
      ["browser.search.suggest.enabled", false],
    ],
  });

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    merinoSuggestions: TEST_MERINO_SUGGESTIONS,
  });
});

add_task(async function basic() {
  for (const merinoSuggestion of TEST_MERINO_SUGGESTIONS) {
    MerinoTestUtils.server.response.body.suggestions = [merinoSuggestion];

    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "only match the Merino suggestion",
    });
    Assert.equal(UrlbarTestUtils.getResultCount(window), 2);

    const { element, result } = await UrlbarTestUtils.getDetailsOfResultAt(
      window,
      1
    );
    const row = element.row;
    const icon = row.querySelector(".urlbarView-favicon");
    Assert.equal(icon.src, merinoSuggestion.icon);
    const url = row.querySelector(".urlbarView-url");
    const expectedUrl = makeExpectedUrl(merinoSuggestion.url);
    const displayUrl = expectedUrl.replace(/^https:\/\//, "");
    Assert.equal(url.textContent, displayUrl);
    const title = row.querySelector(".urlbarView-title");
    Assert.equal(title.textContent, merinoSuggestion.title);
    const description = row.querySelector(".urlbarView-row-body-description");
    Assert.equal(description.textContent, merinoSuggestion.description);
    const bottom = row.querySelector(".urlbarView-row-body-bottom");
    Assert.equal(bottom.textContent, "Recommended");
    Assert.ok(
      BrowserTestUtils.isVisible(
        row.querySelector(".urlbarView-title-separator")
      ),
      "The title separator should be visible"
    );

    Assert.equal(result.suggestedIndex, 1);

    const onLoad = BrowserTestUtils.browserLoaded(
      gBrowser.selectedBrowser,
      false,
      expectedUrl
    );
    EventUtils.synthesizeMouseAtCenter(row, {});
    await onLoad;
    Assert.ok(true, "Expected page is loaded");

    await PlacesUtils.history.clear();
  }
});

add_task(async function resultMenu_showLessFrequently() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.addons.showLessFrequentlyCount", 0]],
  });

  await QuickSuggestTestUtils.setConfig({
    show_less_frequently_cap: 3,
  });

  await doShowLessFrequently({
    input: "aaa b",
    expected: {
      isSuggestionShown: true,
      isMenuItemShown: true,
    },
  });
  Assert.equal(UrlbarPrefs.get("addons.showLessFrequentlyCount"), 1);

  await doShowLessFrequently({
    input: "aaa b",
    expected: {
      isSuggestionShown: true,
      isMenuItemShown: true,
    },
  });
  Assert.equal(UrlbarPrefs.get("addons.showLessFrequentlyCount"), 2);

  // The cap will be reached this time. Keep the view open so we can make sure
  // the command has been removed from the menu before it closes.
  await doShowLessFrequently({
    keepViewOpen: true,
    input: "aaa b",
    expected: {
      isSuggestionShown: true,
      isMenuItemShown: true,
    },
  });
  Assert.equal(UrlbarPrefs.get("addons.showLessFrequentlyCount"), 3);

  // Make sure the command has been removed.
  let menuitem = await UrlbarTestUtils.openResultMenuAndGetItem({
    window,
    command: "show_less_frequently",
    resultIndex: EXPECTED_RESULT_INDEX,
    openByMouse: true,
  });
  Assert.ok(!menuitem, "Menuitem should be absent before closing the view");
  gURLBar.view.resultMenu.hidePopup(true);
  await UrlbarTestUtils.promisePopupClose(window);

  await doShowLessFrequently({
    input: "aaa b",
    expected: {
      // The suggestion should not display since addons.showLessFrequentlyCount
      // is 3 and the substring (" b") after the first word ("aaa") is 2 chars
      // long.
      isSuggestionShown: false,
    },
  });

  await doShowLessFrequently({
    input: "aaa bb",
    expected: {
      // The suggestion should display, but item should not shown since the
      // addons.showLessFrequentlyCount reached to addonsShowLessFrequentlyCap
      // already.
      isSuggestionShown: true,
      isMenuItemShown: false,
    },
  });

  await QuickSuggestTestUtils.setConfig(QuickSuggestTestUtils.DEFAULT_CONFIG);
  await SpecialPowers.popPrefEnv();
});

// Tests the "Not interested" result menu dismissal command.
add_task(async function resultMenu_notInterested() {
  await doDismissTest("not_interested", true);
});

// Tests the "Not relevant" result menu dismissal command.
add_task(async function resultMenu_notRelevant() {
  await doDismissTest("not_relevant", false);
});

// Tests the "Manage" result menu.
add_task(async function resultMenu_manage() {
  await doManageTest({ input: "only match the Merino suggestion", index: 1 });
});

// Tests the row/group label.
add_task(async function rowLabel() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "only match the Merino suggestion",
  });
  Assert.equal(UrlbarTestUtils.getResultCount(window), 2);

  const { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
  const row = element.row;
  Assert.equal(row.getAttribute("label"), "Firefox extension");

  await UrlbarTestUtils.promisePopupClose(window);
});

async function doShowLessFrequently({ input, expected, keepViewOpen = false }) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: input,
  });

  if (!expected.isSuggestionShown) {
    Assert.ok(
      !(await getAddonResultDetails()),
      "Addons suggestion should be absent"
    );
    return;
  }

  const details = await getAddonResultDetails();
  Assert.ok(
    details,
    `Addons suggestion should be present at expected index after ${input} search`
  );

  // Click the command.
  try {
    await UrlbarTestUtils.openResultMenuAndClickItem(
      window,
      "show_less_frequently",
      {
        resultIndex: EXPECTED_RESULT_INDEX,
      }
    );
    Assert.ok(expected.isMenuItemShown);
    Assert.ok(
      gURLBar.view.isOpen,
      "The view should remain open clicking the command"
    );
    Assert.ok(
      details.element.row.hasAttribute("feedback-acknowledgment"),
      "Row should have feedback acknowledgment after clicking command"
    );
  } catch (e) {
    Assert.ok(!expected.isMenuItemShown);
    Assert.ok(
      !details.element.row.hasAttribute("feedback-acknowledgment"),
      "Row should not have feedback acknowledgment after clicking command"
    );
    Assert.equal(
      e.message,
      "Menu item not found for command: show_less_frequently"
    );
  }

  if (!keepViewOpen) {
    await UrlbarTestUtils.promisePopupClose(window);
  }
}

async function doDismissTest(command, allDismissed) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "123",
  });

  const resultCount = UrlbarTestUtils.getResultCount(window);
  let details = await getAddonResultDetails();
  Assert.ok(details, "Addons suggestion should be present");

  // Sanity check.
  Assert.ok(UrlbarPrefs.get("suggest.addons"));

  // Click the command.
  let dismissalPromise = TestUtils.topicObserved(
    "quicksuggest-dismissals-changed"
  );
  await UrlbarTestUtils.openResultMenuAndClickItem(
    window,
    ["[data-l10n-id=firefox-suggest-command-dont-show-this]", command],
    { resultIndex: EXPECTED_RESULT_INDEX, openByMouse: true }
  );
  info("Awaiting dismissal promise");
  await dismissalPromise;

  Assert.equal(
    UrlbarPrefs.get("suggest.addons"),
    !allDismissed,
    "suggest.addons should be true iff all suggestions weren't dismissed"
  );
  Assert.equal(
    await QuickSuggest.isResultDismissed(details.result),
    !allDismissed,
    "Result should be dismissed iff all suggestions weren't dismissed"
  );

  // The row should be a tip now.
  Assert.ok(gURLBar.view.isOpen, "The view should remain open after dismissal");
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    resultCount,
    "The result count should not haved changed after dismissal"
  );
  details = await UrlbarTestUtils.getDetailsOfResultAt(
    window,
    EXPECTED_RESULT_INDEX
  );
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

  // Check tip title.
  let title = details.element.row.querySelector(".urlbarView-title");
  let titleL10nId = title.dataset.l10nId;
  if (allDismissed) {
    Assert.equal(titleL10nId, "firefox-suggest-dismissal-acknowledgment-all");
  } else {
    Assert.equal(titleL10nId, "firefox-suggest-dismissal-acknowledgment-one");
  }

  // Get the dismissal acknowledgment's "Got it" button and click it.
  let gotItButton = UrlbarTestUtils.getButtonForResultIndex(
    window,
    0,
    EXPECTED_RESULT_INDEX
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
        !isAddonResult(details.result),
      "Tip result and addon result should not be present"
    );
  }

  await UrlbarTestUtils.promisePopupClose(window);

  UrlbarPrefs.clear("suggest.addons");
  await QuickSuggest.clearDismissedSuggestions();
}

function makeExpectedUrl(originalUrl) {
  let url = new URL(originalUrl);
  url.searchParams.set("utm_medium", "firefox-desktop");
  url.searchParams.set("utm_source", "firefox-suggest");
  return url.href;
}

async function getAddonResultDetails() {
  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    const details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    if (isAddonResult(details.result)) {
      return details;
    }
  }
  return null;
}

function isAddonResult(result) {
  return ["AddonSuggestions", "amo"].includes(result.payload.provider);
}
