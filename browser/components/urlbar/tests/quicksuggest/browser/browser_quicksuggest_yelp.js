/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for Yelp suggestions.

const REMOTE_SETTINGS_RECORDS = [
  {
    type: "yelp-suggestions",
    attachment: {
      subjects: ["ramen"],
      businessSubjects: ["the shop"],
      preModifiers: ["best"],
      postModifiers: ["delivery"],
      locationSigns: ["in"],
      yelpModifiers: [],
      icon: "1234",
      score: 0.5,
    },
  },
  ...QuickSuggestTestUtils.geonamesRecords(),
  ...QuickSuggestTestUtils.geonamesAlternatesRecords(),
];

add_setup(async function () {
  Services.prefs.setBoolPref("browser.search.suggest.enabled", false);

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: REMOTE_SETTINGS_RECORDS,
    prefs: [
      ["suggest.quicksuggest.sponsored", true],
      ["suggest.yelp", true],
      ["yelp.featureGate", true],
      ["yelp.serviceResultDistinction", true],
    ],
  });
});

add_task(async function basic() {
  for (let topPick of [true, false]) {
    info("Setting yelpPriority: " + topPick);
    await SpecialPowers.pushPrefEnv({
      set: [["browser.urlbar.quicksuggest.yelpPriority", topPick]],
    });

    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "RaMeN iN tOkYo",
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
    Assert.equal(result.payload.provider, "Yelp");
    Assert.equal(
      result.payload.url,
      "https://www.yelp.com/search?find_desc=RaMeN&find_loc=Tokyo%2C+Tokyo-to&utm_medium=partner&utm_source=mozilla"
    );

    const { row } = element;
    Assert.ok(row.hasAttribute("sponsored"));
    const icon = row.querySelector(".urlbarView-favicon");
    Assert.equal(icon.src, "chrome://global/skin/icons/defaultFavicon.svg");
    const title = row.querySelector(".urlbarView-title");
    Assert.equal(title.textContent, "Top results for RaMeN iN Tokyo, Tokyo-to");
    const subtitle = row.querySelector(".urlbarView-subtitle");
    Assert.equal(subtitle.textContent, "Yelp");
    const description = row.querySelector(".urlbarView-row-body-description");
    Assert.equal(description.textContent, "");
    const bottomLabel = row.querySelector(".urlbarView-bottom-label");
    Assert.equal(bottomLabel.textContent, "Sponsored");
    const bottomUrl = row.querySelector(".urlbarView-url");
    Assert.equal(
      bottomUrl.textContent,
      "yelp.com/search?find_desc=RaMeN&find_loc=Tokyo,+Tokyo-to&utm_medium=partner&utm_source=mozilla"
    );

    await UrlbarTestUtils.promisePopupClose(window);
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function businessSubject() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.quicksuggest.yelpPriority", true]],
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "the shop to",
  });
  Assert.equal(UrlbarTestUtils.getResultCount(window), 2);

  const details = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
  const { element, result } = details;
  Assert.equal(
    result.providerName,
    UrlbarProviderQuickSuggest.name,
    "The result should be from the expected provider"
  );
  Assert.equal(result.payload.provider, "Yelp");
  Assert.equal(
    result.payload.url,
    "https://www.yelp.com/search?find_desc=the+shop&find_loc=Tokyo%2C+Tokyo-to&utm_medium=partner&utm_source=mozilla"
  );
  const titleElement = element.row.querySelector(".urlbarView-title");
  Assert.equal(titleElement.textContent, "the shop in Tokyo, Tokyo-to");

  await UrlbarTestUtils.promisePopupClose(window);
  await SpecialPowers.popPrefEnv();
});

// Tests the "Show less frequently" result menu command.
add_task(async function resultMenu_show_less_frequently() {
  info("Test for no yelpMinKeywordLength and no yelpShowLessFrequentlyCap");
  await doShowLessFrequently({
    minKeywordLength: 0,
    frequentlyCap: 0,
    testData: [
      {
        input: "best ra",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: true,
        },
      },
      {
        input: "best ra",
        expected: {
          hasSuggestion: false,
        },
      },
      {
        input: "best ram",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: true,
        },
      },
      {
        input: "best ram",
        expected: {
          hasSuggestion: false,
        },
      },
      {
        input: "best rame",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: true,
        },
      },
      {
        input: "best rame",
        expected: {
          hasSuggestion: false,
        },
      },
    ],
  });

  info("Test whether yelpShowLessFrequentlyCap can work");
  await doShowLessFrequently({
    minKeywordLength: 0,
    frequentlyCap: 2,
    testData: [
      {
        input: "best ra",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: true,
        },
      },
      {
        input: "best ram",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: true,
        },
      },
      {
        input: "best ram",
        expected: {
          hasSuggestion: false,
        },
      },
      {
        input: "best rame",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: false,
        },
      },
      {
        input: "best ramen",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: false,
        },
      },
    ],
  });

  info(
    "Test whether local yelp.minKeywordLength pref can override nimbus variable yelpMinKeywordLength"
  );
  await doShowLessFrequently({
    minKeywordLength: 8,
    frequentlyCap: 0,
    testData: [
      {
        input: "best ra",
        expected: {
          hasSuggestion: false,
        },
      },
      {
        input: "best ram",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: true,
        },
      },
      {
        input: "best rame",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: true,
        },
      },
      {
        input: "best rame",
        expected: {
          hasSuggestion: false,
        },
      },
    ],
  });
});

async function doShowLessFrequently({
  minKeywordLength,
  frequentlyCap,
  testData,
}) {
  UrlbarPrefs.clear("yelp.showLessFrequentlyCount");
  UrlbarPrefs.clear("yelp.minKeywordLength");

  let cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
    yelpMinKeywordLength: minKeywordLength,
    yelpShowLessFrequentlyCap: frequentlyCap,
  });

  for (let { input, expected } of testData) {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: input,
    });

    if (expected.hasSuggestion) {
      let resultIndex = 1;
      let details = await UrlbarTestUtils.getDetailsOfResultAt(
        window,
        resultIndex
      );
      Assert.equal(details.result.payload.provider, "Yelp");

      if (expected.hasShowLessItem) {
        // Click the command.
        let previousShowLessFrequentlyCount = UrlbarPrefs.get(
          "yelp.showLessFrequentlyCount"
        );
        await UrlbarTestUtils.openResultMenuAndClickItem(
          window,
          "show_less_frequently",
          { resultIndex, openByMouse: true }
        );

        Assert.equal(
          UrlbarPrefs.get("yelp.showLessFrequentlyCount"),
          previousShowLessFrequentlyCount + 1
        );
        Assert.equal(
          UrlbarPrefs.get("yelp.minKeywordLength"),
          input.length + 1
        );
      } else {
        let menuitem = await UrlbarTestUtils.openResultMenuAndGetItem({
          window,
          command: "show_less_frequently",
          resultIndex: 1,
          openByMouse: true,
        });
        Assert.ok(!menuitem);
      }
    } else {
      // Yelp suggestion should not be shown.
      for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
        let details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
        Assert.notEqual(details.result.payload.provider, "Yelp");
      }
    }

    await UrlbarTestUtils.promisePopupClose(window);
  }

  await cleanUpNimbus();
  UrlbarPrefs.clear("yelp.showLessFrequentlyCount");
  UrlbarPrefs.clear("yelp.minKeywordLength");
}

// Tests the "Dismiss" result menu dismissal command.
add_task(async function resultMenu_dismiss() {
  await doDismiss({
    menu: "dismiss",
    expectDismissalAcknowledgment: false,
    assert: result => {
      Assert.ok(
        QuickSuggest.isResultDismissed(result),
        "The result should be dismissed"
      );
    },
  });

  await QuickSuggest.clearDismissedSuggestions();
});

async function doDismiss({ menu, assert, expectDismissalAcknowledgment }) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "ramen",
  });

  let resultCount = UrlbarTestUtils.getResultCount(window);
  let resultIndex = 1;
  let details = await UrlbarTestUtils.getDetailsOfResultAt(window, resultIndex);
  Assert.equal(details.result.payload.provider, "Yelp");
  let result = details.result;

  // Click the command.
  let dismissalPromise = TestUtils.topicObserved(
    "quicksuggest-dismissals-changed"
  );
  await UrlbarTestUtils.openResultMenuAndClickItem(window, [menu], {
    resultIndex,
    openByMouse: true,
  });
  info("Awaiting dismissal promise");
  await dismissalPromise;

  Assert.ok(gURLBar.view.isOpen, "The view should remain open after dismissal");

  if (expectDismissalAcknowledgment) {
    // The row should be a tip now.
    Assert.equal(
      UrlbarTestUtils.getResultCount(window),
      resultCount,
      "The result count should not haved changed after dismissal"
    );
    details = await UrlbarTestUtils.getDetailsOfResultAt(window, resultIndex);
    Assert.equal(
      details.type,
      UrlbarShared.RESULT_TYPE.TIP,
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
    let gotItButton = UrlbarTestUtils.getButtonForResultIndex(
      window,
      "0",
      resultIndex
    );
    Assert.ok(gotItButton, "Row should have a 'Got it' button");
    EventUtils.synthesizeMouseAtCenter(gotItButton, {}, window);
  }

  // The view should remain open and the tip row should be gone.
  Assert.ok(
    gURLBar.view.isOpen,
    "The view should remain open after dismissal and acknowledgment"
  );
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    resultCount - 1,
    "The result count should be one less after dismissal and acknowledgment"
  );

  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    Assert.ok(
      details.type != UrlbarShared.RESULT_TYPE.TIP &&
        details.result.payload.provider !== "Yelp",
      "Tip result and Yelp result should not be present"
    );
  }

  assert(result);

  await UrlbarTestUtils.promisePopupClose(window);

  // Check that the result should not be shown anymore.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "ramen",
  });

  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    Assert.notStrictEqual(
      details.result.payload.provider,
      "Yelp",
      "Yelp result should not be present"
    );
  }

  await UrlbarTestUtils.promisePopupClose(window);
}

// Tests the "Manage" result menu.
add_task(async function resultMenu_manage() {
  await doManageTest({ input: "ramen", index: 1 });
});

// Tests the "Learn more" result menu.
add_task(async function resultMenu_learn_more() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "ramen",
  });

  info("Selecting Learn more item from the result menu");
  let tabOpenPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    Services.urlFormatter.formatURLPref("app.support.baseURL") +
      "awesome-bar-result-menu"
  );
  await UrlbarTestUtils.openResultMenuAndClickItem(window, "help", {
    resultIndex: 1,
  });
  info("Waiting for Learn more link to open in a new tab");
  await tabOpenPromise;
  gBrowser.removeCurrentTab();

  await UrlbarTestUtils.promisePopupClose(window);
});

// Tests the row/group label.
add_task(async function rowLabel() {
  let tests = [
    { topPick: true, label: null },
    { topPick: false, label: "Firefox Suggest" },
  ];

  for (let { topPick, label } of tests) {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.urlbar.yelp.priority", topPick]],
    });

    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "ramen",
    });
    Assert.equal(UrlbarTestUtils.getResultCount(window), 2);

    const { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
    const row = element.row;
    Assert.equal(row.getAttribute("label"), label);

    await UrlbarTestUtils.promisePopupClose(window);
    await SpecialPowers.popPrefEnv();
  }
});
