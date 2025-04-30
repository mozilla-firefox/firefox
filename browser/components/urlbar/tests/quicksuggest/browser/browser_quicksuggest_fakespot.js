/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for Fakespot suggestions.

// Trying to avoid CI timeouts in verify (TV) mode, especially on Mac.
requestLongerTimeout(5);

const REMOTE_SETTINGS_RECORDS = [
  {
    type: "icon",
    id: "icon-fakespot-amazon",
    attachmentMimetype: "image/png",
    attachment: [1, 2, 3],
  },
  {
    type: "icon",
    id: "icon-fakespot-bestbuy",
    attachmentMimetype: "image/svg+xml",
    attachment: [4, 5, 6],
  },
  {
    collection: "fakespot-suggest-products",
    type: "fakespot-suggestions",
    attachment: [
      {
        url: "https://example.com/maybe-good-item",
        title: "Maybe Good Item",
        rating: 4.8,
        total_reviews: 1234567,
        fakespot_grade: "A",
        product_id: "amazon-0",
        score: 0.01,
        keywords: "",
        product_type: "",
      },
      {
        url: "https://example.com/1-review-item",
        title: "1 review item",
        rating: 5,
        total_reviews: 1,
        fakespot_grade: "A",
        product_id: "amazon-1",
        score: 0.1,
        keywords: "",
        product_type: "",
      },
      {
        url: "https://example.com/2-reviews-item",
        title: "2 reviews item",
        rating: 4,
        total_reviews: 2,
        fakespot_grade: "A",
        product_id: "amazon-2",
        score: 0.2,
        keywords: "",
        product_type: "",
      },
      {
        url: "https://example.com/1000-reviews-item",
        title: "1000 reviews item",
        rating: 3,
        total_reviews: 1000,
        fakespot_grade: "A",
        product_id: "amazon-3",
        score: 0.3,
        keywords: "",
        product_type: "",
      },
      {
        url: "https://example.com/99999-reviews-item",
        title: "99999 reviews item",
        rating: 2,
        total_reviews: 99999,
        fakespot_grade: "A",
        product_id: "amazon-4",
        score: 0.4,
        keywords: "",
        product_type: "",
      },
      {
        url: "https://example.com/100000-reviews-item",
        title: "100000 reviews item",
        rating: 1,
        total_reviews: 100000,
        fakespot_grade: "A",
        product_id: "amazon-5",
        score: 0.5,
        keywords: "",
        product_type: "",
      },
      {
        url: "https://example.com/png-image-item",
        title: "png image item",
        rating: 5,
        total_reviews: 1,
        fakespot_grade: "A",
        product_id: "amazon-6",
        score: 0.6,
        keywords: "",
        product_type: "",
      },
      {
        url: "https://example.com/svg-image-item",
        title: "svg image item",
        rating: 5,
        total_reviews: 1,
        fakespot_grade: "A",
        product_id: "bestbuy-1",
        score: 0.7,
        keywords: "",
        product_type: "",
      },
      {
        url: "https://example.com/no-image-item",
        title: "no image item",
        rating: 5,
        total_reviews: 1,
        fakespot_grade: "A",
        product_id: "walmart-1",
        score: 0.8,
        keywords: "",
        product_type: "",
      },
    ],
  },
];

const HELP_URL =
  Services.urlFormatter.formatURLPref("app.support.baseURL") +
  "awesome-bar-result-menu";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.search.suggest.enabled", false],
      ["browser.urlbar.suggest.quickactions", false],
    ],
  });

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: REMOTE_SETTINGS_RECORDS,
    prefs: [
      ["suggest.quicksuggest.sponsored", true],
      ["suggest.fakespot", true],
      ["fakespot.featureGate", true],
    ],
  });

  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
  });
});

add_task(async function basic() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "Maybe",
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
    Assert.equal(result.payload.provider, "Fakespot");

    const onLoad = BrowserTestUtils.browserLoaded(
      gBrowser.selectedBrowser,
      false,
      "https://example.com/maybe-good-item"
    );
    EventUtils.synthesizeMouseAtCenter(element.row, {});
    await onLoad;
    Assert.ok(true, "Expected page is loaded");
  });

  await PlacesUtils.history.clear();
});

// Tests the "Show less frequently" result menu command.
add_task(async function resultMenu_show_less_frequently() {
  info(
    "Test for no fakespotMinKeywordLength and no fakespotShowLessFrequentlyCap"
  );
  await doShowLessFrequently({
    minKeywordLength: 0,
    frequentlyCap: 0,
    testData: [
      {
        input: "maybe",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: true,
        },
      },
      {
        input: "maybe",
        expected: {
          hasSuggestion: false,
        },
      },
      {
        input: "maybe g",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: true,
        },
      },
      {
        input: "maybe g",
        expected: {
          hasSuggestion: false,
        },
      },
      {
        input: "maybe go",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: true,
        },
      },
      {
        input: "maybe go",
        expected: {
          hasSuggestion: false,
        },
      },
    ],
  });

  info("Test whether fakespotShowLessFrequentlyCap can work");
  await doShowLessFrequently({
    minKeywordLength: 0,
    frequentlyCap: 2,
    testData: [
      {
        input: "maybe g",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: true,
        },
      },
      {
        input: "maybe go",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: true,
        },
      },
      {
        input: "maybe go",
        expected: {
          hasSuggestion: false,
        },
      },
      {
        input: "maybe goo",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: false,
        },
      },
      {
        input: "maybe good",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: false,
        },
      },
    ],
  });

  info(
    "Test whether local fakespot.minKeywordLength pref can override nimbus variable fakespotMinKeywordLength"
  );
  await doShowLessFrequently({
    minKeywordLength: 8,
    frequentlyCap: 0,
    testData: [
      {
        input: "maybe g",
        expected: {
          hasSuggestion: false,
        },
      },
      {
        input: "maybe go",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: true,
        },
      },
      {
        input: "maybe goo",
        expected: {
          hasSuggestion: true,
          hasShowLessItem: true,
        },
      },
      {
        input: "maybe goo",
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
  UrlbarPrefs.clear("fakespot.showLessFrequentlyCount");
  UrlbarPrefs.clear("fakespot.minKeywordLength");

  let cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
    fakespotMinKeywordLength: minKeywordLength,
    fakespotShowLessFrequentlyCap: frequentlyCap,
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
      Assert.equal(details.result.payload.provider, "Fakespot");

      if (expected.hasShowLessItem) {
        // Click the command.
        let previousShowLessFrequentlyCount = UrlbarPrefs.get(
          "fakespot.showLessFrequentlyCount"
        );
        await UrlbarTestUtils.openResultMenuAndClickItem(
          window,
          "show_less_frequently",
          { resultIndex, openByMouse: true }
        );

        Assert.equal(
          UrlbarPrefs.get("fakespot.showLessFrequentlyCount"),
          previousShowLessFrequentlyCount + 1
        );
        Assert.equal(
          UrlbarPrefs.get("fakespot.minKeywordLength"),
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
      // Fakespot suggestion should not be shown.
      for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
        let details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
        Assert.notEqual(details.result.payload.provider, "Fakespot");
      }
    }

    await UrlbarTestUtils.promisePopupClose(window);
  }

  await cleanUpNimbus();
  UrlbarPrefs.clear("fakespot.showLessFrequentlyCount");
  UrlbarPrefs.clear("fakespot.minKeywordLength");
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
      Assert.ok(!UrlbarPrefs.get("suggest.fakespot"));
    },
  });

  UrlbarPrefs.clear("suggest.fakespot");
});

async function doDismiss({ menu, assert }) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "maybe",
  });

  let resultCount = UrlbarTestUtils.getResultCount(window);
  let resultIndex = 1;
  let details = await UrlbarTestUtils.getDetailsOfResultAt(window, resultIndex);
  Assert.equal(details.result.payload.provider, "Fakespot");
  let result = details.result;

  // Click the command.
  let dismissalPromise = TestUtils.topicObserved(
    "quicksuggest-dismissals-changed"
  );
  await UrlbarTestUtils.openResultMenuAndClickItem(
    window,
    ["[data-l10n-id=firefox-suggest-command-manage-fakespot]", menu],
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
        details.result.payload.provider !== "Fakespot",
      "Tip result and Fakespot result should not be present"
    );
  }

  assert(result);

  await UrlbarTestUtils.promisePopupClose(window);

  // Check that the result should not be shown anymore.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "maybe",
  });

  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    Assert.ok(
      details.result.payload.provider !== "Fakespot",
      "Fakespot result should not be present"
    );
  }

  await UrlbarTestUtils.promisePopupClose(window);
}

// Tests the "Manage" result menu.
add_task(async function resultMenu_manage() {
  await doManageTest({ input: "maybe", index: 1 });
});

// Tests the "Learn more" (a.k.a. "Help") result menu command.
add_task(async function resultMenu_help() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "maybe",
  });

  let resultIndex = 1;
  let details = await UrlbarTestUtils.getDetailsOfResultAt(window, resultIndex);
  Assert.equal(details.result.payload.provider, "Fakespot");

  let loadPromise = BrowserTestUtils.waitForNewTab(gBrowser);

  await UrlbarTestUtils.openResultMenuAndClickItem(window, "help", {
    resultIndex,
    openByMouse: true,
  });

  info("Waiting for the help page to load");
  await loadPromise;
  await TestUtils.waitForTick();
  Assert.equal(
    gBrowser.currentURI.spec,
    HELP_URL,
    "The loaded URL should be the help URL"
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// Test the label for rating and total reviews.
add_task(async function ratingAndTotalReviewsLabel() {
  const testData = [
    { input: "1 review", expected: "5 · (1 review)" },
    { input: "2 reviews", expected: "4 · (2 reviews)" },
    { input: "1000 reviews", expected: "3 · (1,000 reviews)" },
    { input: "99999 reviews", expected: "2 · (99,999 reviews)" },
    { input: "100000 reviews", expected: "1 · (99,999+ reviews)" },
  ];

  for (const { input, expected } of testData) {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: input,
    });
    Assert.equal(UrlbarTestUtils.getResultCount(window), 2);

    const { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
    Assert.equal(
      element.row.querySelector(
        ".urlbarView-dynamic-fakespot-rating-and-total-reviews"
      ).textContent,
      expected
    );

    await UrlbarTestUtils.promisePopupClose(window);
  }
});

// Test the icons.
add_task(async function icons() {
  const testData = [
    {
      input: "png image",
      expectedIcon: REMOTE_SETTINGS_RECORDS.find(
        r => r.id == "icon-fakespot-amazon"
      ),
    },
    {
      input: "svg image",
      expectedIcon: REMOTE_SETTINGS_RECORDS.find(
        r => r.id == "icon-fakespot-bestbuy"
      ),
    },
    { input: "no image", expectedIcon: null },
  ];

  for (const { input, expectedIcon } of testData) {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: input,
    });
    Assert.equal(UrlbarTestUtils.getResultCount(window), 2);

    const { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
    const src = element.row.querySelector(
      ".urlbarView-dynamic-fakespot-icon"
    ).src;

    if (!expectedIcon) {
      Assert.equal(src, "");
      return;
    }

    const content = await fetch(src);
    const blob = await content.blob();
    const bytes = await blob.bytes();

    Assert.equal(blob.type, expectedIcon.attachmentMimetype);
    Assert.equal(
      new TextDecoder().decode(bytes),
      JSON.stringify(expectedIcon.attachment)
    );

    await UrlbarTestUtils.promisePopupClose(window);
  }
});
