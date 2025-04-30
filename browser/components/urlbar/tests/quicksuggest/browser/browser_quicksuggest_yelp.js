/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for Yelp suggestions.

const REMOTE_SETTINGS_RECORDS = [
  {
    type: "yelp-suggestions",
    attachment: {
      subjects: ["ramen"],
      preModifiers: ["best"],
      postModifiers: ["delivery"],
      locationSigns: ["in"],
      yelpModifiers: [],
      icon: "1234",
      score: 0.5,
    },
  },
  QuickSuggestTestUtils.geonamesRecord(),
];

add_setup(async function () {
  Services.prefs.setBoolPref("browser.search.suggest.enabled", false);

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: REMOTE_SETTINGS_RECORDS,
    prefs: [
      ["suggest.quicksuggest.sponsored", true],
      ["suggest.yelp", true],
      ["yelp.featureGate", true],
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

    const details = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
    const { result } = details;
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
    Assert.equal(result.payload.title, "RaMeN iN Tokyo, Tokyo-to");

    const { row } = details.element;
    const bottom = row.querySelector(".urlbarView-row-body-bottom");
    Assert.ok(bottom, "Bottom text element should exist");
    Assert.ok(
      BrowserTestUtils.isVisible(bottom),
      "Bottom text element should be visible"
    );
    Assert.equal(
      bottom.textContent,
      "Yelp · Sponsored",
      "Bottom text is correct"
    );

    await UrlbarTestUtils.promisePopupClose(window);
    await SpecialPowers.popPrefEnv();
  }
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

// Tests the "Not relevant" result menu dismissal command.
add_task(async function resultMenu_not_relevant() {
  await doDismiss({
    menu: "not_relevant",
    assert: result => {
      Assert.ok(
        QuickSuggest.isResultDismissed(result),
        "The result should be dismissed"
      );
    },
  });

  await QuickSuggest.clearDismissedSuggestions();
});

// Tests the "Not interested" result menu dismissal command.
add_task(async function resultMenu_not_interested() {
  await doDismiss({
    menu: "not_interested",
    assert: () => {
      Assert.ok(!UrlbarPrefs.get("suggest.yelp"));
    },
  });

  UrlbarPrefs.clear("suggest.yelp");
});

async function doDismiss({ menu, assert }) {
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
  await UrlbarTestUtils.openResultMenuAndClickItem(
    window,
    ["[data-l10n-id=firefox-suggest-command-dont-show-this]", menu],
    {
      resultIndex,
      openByMouse: true,
    }
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
  let gotItButton = UrlbarTestUtils.getButtonForResultIndex(
    window,
    "0",
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
    Assert.ok(
      details.result.payload.provider !== "Yelp",
      "Yelp result should not be present"
    );
  }

  await UrlbarTestUtils.promisePopupClose(window);
}

// Tests the "Manage" result menu.
add_task(async function resultMenu_manage() {
  await doManageTest({ input: "ramen", index: 1 });
});

// Tests the row/group label.
add_task(async function rowLabel() {
  let tests = [
    { topPick: true, label: "Local recommendations" },
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
