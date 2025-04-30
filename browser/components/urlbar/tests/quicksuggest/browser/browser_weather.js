/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Browser test for the weather suggestion.
 */

"use strict";

const EXPECTED_RESULT_INDEX = 1;

const { WEATHER_SUGGESTION } = MerinoTestUtils;

// This test takes a while and can time out in verify mode.
requestLongerTimeout(5);

add_setup(async function () {
  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: [QuickSuggestTestUtils.weatherRecord()],
    prefs: [
      ["suggest.quicksuggest.sponsored", true],
      ["weather.featureGate", true],
    ],
  });
  await MerinoTestUtils.initWeather();
});

// Does a search, clicks the "Show less frequently" result menu command, and
// repeats both steps until the min keyword length cap is reached.
add_task(async function showLessFrequentlyCapReached_manySearches() {
  // Set up a min keyword length and cap.
  await QuickSuggestTestUtils.setRemoteSettingsRecords([
    QuickSuggestTestUtils.weatherRecord({
      min_keyword_length: 3,
    }),
    {
      type: "configuration",
      configuration: {
        show_less_frequently_cap: 1,
      },
    },
  ]);

  // Trigger the suggestion.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "wea",
  });

  info("Weather suggestion should be present after 'wea' search");
  let details = await assertWeatherResultPresent();

  // Click the command.
  let command = "show_less_frequently";
  await UrlbarTestUtils.openResultMenuAndClickItem(window, command, {
    resultIndex: EXPECTED_RESULT_INDEX,
  });

  Assert.ok(
    gURLBar.view.isOpen,
    "The view should remain open clicking the command"
  );
  Assert.ok(
    details.element.row.hasAttribute("feedback-acknowledgment"),
    "Row should have feedback acknowledgment after clicking command"
  );
  Assert.equal(
    UrlbarPrefs.get("weather.minKeywordLength"),
    4,
    "weather.minKeywordLength should be incremented once"
  );

  // Do the same search again. The suggestion should not appear.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "wea",
  });

  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    info(`Weather suggestion should be absent (checking index ${i})`);
    assertIsWeatherResult(details.result, false);
  }

  // Do a search using one more character. The suggestion should appear.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "weat",
  });

  info("Weather suggestion should be present after 'weat' search");
  details = await assertWeatherResultPresent();
  Assert.ok(
    !details.element.row.hasAttribute("feedback-acknowledgment"),
    "Row should not have feedback acknowledgment after 'weat' search"
  );

  // Since the cap has been reached, the command should no longer appear in the
  // result menu.
  await UrlbarTestUtils.openResultMenu(window, {
    resultIndex: EXPECTED_RESULT_INDEX,
  });
  let menuitem = gURLBar.view.resultMenu.querySelector(
    `menuitem[data-command=${command}]`
  );
  Assert.ok(!menuitem, "Menuitem should be absent");
  gURLBar.view.resultMenu.hidePopup(true);

  await UrlbarTestUtils.promisePopupClose(window);
  await QuickSuggestTestUtils.setRemoteSettingsRecords([
    QuickSuggestTestUtils.weatherRecord(),
  ]);
  UrlbarPrefs.clear("weather.minKeywordLength");
  UrlbarPrefs.clear("weather.showLessFrequentlyCount");
});

// Tests the "Don't show weather suggestions" result menu dismissal command.
add_task(async function dontShow() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "weather",
  });
  await doDismissTest("dismiss");
});

async function doDismissTest(command) {
  let resultCount = UrlbarTestUtils.getResultCount(window);
  let details = await assertWeatherResultPresent();

  // Click the command.
  let dismissalPromise = TestUtils.topicObserved(
    "quicksuggest-dismissals-changed"
  );
  await UrlbarTestUtils.openResultMenuAndClickItem(window, command, {
    resultIndex: EXPECTED_RESULT_INDEX,
    openByMouse: true,
  });
  info("Awaiting dismissal promise");
  await dismissalPromise;

  Assert.ok(
    !UrlbarPrefs.get("suggest.weather"),
    "suggest.weather pref should be set to false after dismissal"
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

  // Get the dismissal acknowledgment's "Got it" button and click it.
  let gotItButton = UrlbarTestUtils.getButtonForResultIndex(
    window,
    "0",
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
    Assert.notEqual(
      details.type,
      UrlbarUtils.RESULT_TYPE.TIP,
      "Tip result should not be present"
    );
    info("Weather result should not be present");
    assertIsWeatherResult(details.result, false);
  }

  await UrlbarTestUtils.promisePopupClose(window);

  // Enable the weather suggestion again.
  UrlbarPrefs.clear("suggest.weather");

  // Wait for keywords to be re-synced from remote settings.
  await QuickSuggestTestUtils.forceSync();
}

// Tests the "Report inaccurate location" result menu command immediately
// followed by a dismissal command to make sure other commands still work
// properly while the urlbar session remains ongoing.
add_task(async function inaccurateLocationAndDismissal() {
  await doSessionOngoingCommandTest("inaccurate_location");
});

// Tests the "Show less frequently" result menu command immediately followed by
// a dismissal command to make sure other commands still work properly while the
// urlbar session remains ongoing.
add_task(async function showLessFrequentlyAndDismissal() {
  await doSessionOngoingCommandTest("show_less_frequently");
  UrlbarPrefs.clear("weather.minKeywordLength");
  UrlbarPrefs.clear("weather.showLessFrequentlyCount");
});

async function doSessionOngoingCommandTest(command) {
  // Trigger the suggestion.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "weather",
  });

  info("Weather suggestion should be present after search");
  let details = await assertWeatherResultPresent();

  // Click the command.
  await UrlbarTestUtils.openResultMenuAndClickItem(window, command, {
    resultIndex: EXPECTED_RESULT_INDEX,
  });

  Assert.ok(
    gURLBar.view.isOpen,
    "The view should remain open clicking the command"
  );
  Assert.ok(
    details.element.row.hasAttribute("feedback-acknowledgment"),
    "Row should have feedback acknowledgment after clicking command"
  );

  info("Doing dismissal");
  await doDismissTest("dismiss");
}

// Test for menu item to manage the suggest.
add_task(async function manage() {
  await BrowserTestUtils.withNewTab({ gBrowser }, async browser => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "weather",
    });

    await assertWeatherResultPresent();

    const managePage = "about:preferences#search";
    let onManagePageLoaded = BrowserTestUtils.browserLoaded(
      browser,
      false,
      managePage
    );
    // Click the command.
    await UrlbarTestUtils.openResultMenuAndClickItem(window, "manage", {
      resultIndex: EXPECTED_RESULT_INDEX,
    });
    await onManagePageLoaded;
    Assert.equal(
      browser.currentURI.spec,
      managePage,
      "The manage page is loaded"
    );

    await UrlbarTestUtils.promisePopupClose(window);
  });
});

// Tests the "simplest" (integer value 0) UI treatment.
add_task(async function simplestUi() {
  let nimbusVariablesList = [
    // The simplest UI should be enabled by default, when no Nimbus experiment
    // is installed.
    null,
    { weatherUiTreatment: 0 },
  ];

  for (let nimbusVariables of nimbusVariablesList) {
    let nimbusCleanup = nimbusVariables
      ? await UrlbarTestUtils.initNimbusFeature(nimbusVariables)
      : null;

    await BrowserTestUtils.withNewTab("about:blank", async () => {
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "weather",
      });

      let details = await assertWeatherResultPresent();

      let { row } = details.element;

      // Build the expected title.
      let unit = Services.locale.regionalPrefsLocales[0] == "en-US" ? "f" : "c";
      let temperature =
        WEATHER_SUGGESTION.current_conditions.temperature[unit] +
        "°" +
        unit.toUpperCase();
      let expectedTitle = [
        temperature,
        "in",
        WEATHER_SUGGESTION.city_name + ",",
        WEATHER_SUGGESTION.region_code,
      ].join(" ");

      // Check the title. L10n string translation is async so it may not be
      // ready yet, so wait for it.
      let title = row.querySelector(".urlbarView-title");
      await TestUtils.waitForCondition(
        () => title.textContent == expectedTitle,
        "Waiting for the row's title text to be updated"
      );
      Assert.equal(
        title.textContent,
        expectedTitle,
        "Row should have expected title"
      );

      // The temperature should be `<strong>`.
      let strongChildren = title.querySelectorAll("strong");
      Assert.equal(
        strongChildren.length,
        1,
        "Title should have one <strong> child"
      );
      Assert.equal(
        strongChildren[0].textContent,
        temperature,
        "Strong child should be correct"
      );

      Assert.ok(
        !row.hasAttribute("label"),
        "Simplest UI should not have a row label"
      );

      await assertPageLoad();
    });

    await nimbusCleanup?.();
  }
});

// Tests the "simpler" (integer value 1) and "full" (2) UI treatments.
add_task(async function simplerAndFullUi() {
  const testData = [
    // simpler
    {
      nimbusVariables: { weatherUiTreatment: 1 },
      expectedSummary: WEATHER_SUGGESTION.current_conditions.summary,
    },
    // full
    {
      nimbusVariables: { weatherUiTreatment: 2 },
      expectedSummary: `${WEATHER_SUGGESTION.current_conditions.summary}; ${WEATHER_SUGGESTION.forecast.summary}`,
    },
  ];

  for (let { nimbusVariables, expectedSummary } of testData) {
    let nimbusCleanup = nimbusVariables
      ? await UrlbarTestUtils.initNimbusFeature(nimbusVariables)
      : null;

    await BrowserTestUtils.withNewTab("about:blank", async () => {
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "weather",
      });

      let details = await assertWeatherResultPresent();

      let { row } = details.element;
      let summary = row.querySelector(
        ".urlbarView-dynamic-weather-summaryText"
      );

      // `getViewUpdate()` is allowed to be async and `UrlbarView` awaits it even
      // though the `Weather` implementation is not async. That means the summary
      // text content will be updated asyncly, so we need to wait for it.
      await TestUtils.waitForCondition(
        () => summary.textContent == expectedSummary,
        "Waiting for the row's summary text to be updated"
      );
      Assert.equal(
        summary.textContent,
        expectedSummary,
        "The summary text should be correct"
      );

      // Check the title too while we're here.
      let expectedTitle = [
        "Weather for",
        WEATHER_SUGGESTION.city_name + ",",
        WEATHER_SUGGESTION.region_code,
      ].join(" ");
      let title = row.querySelector(".urlbarView-dynamic-weather-title");
      await TestUtils.waitForCondition(
        () => title.textContent == expectedTitle,
        "Waiting for the row's title text to be updated"
      );
      Assert.equal(
        title.textContent,
        expectedTitle,
        "The title text should be correct"
      );

      Assert.equal(
        row.getAttribute("label"),
        "Firefox Suggest",
        "Row label should be correct"
      );

      await assertPageLoad();
    });

    await nimbusCleanup?.();
  }
});

async function assertWeatherResultPresent() {
  let details = await UrlbarTestUtils.getDetailsOfResultAt(
    window,
    EXPECTED_RESULT_INDEX
  );
  assertIsWeatherResult(details.result, true);
  return details;
}

function assertIsWeatherResult(result, isWeatherResult) {
  if (isWeatherResult) {
    Assert.equal(
      result.providerName,
      UrlbarProviderQuickSuggest.name,
      "Result should be from UrlbarProviderQuickSuggest"
    );
    Assert.equal(
      UrlbarUtils.searchEngagementTelemetryType(result),
      "weather",
      "Result telemetry type should be 'weather'"
    );
  } else {
    Assert.notEqual(
      result.providerName,
      UrlbarProviderQuickSuggest.name,
      "Result should not be from UrlbarProviderQuickSuggest"
    );
    Assert.notEqual(
      UrlbarUtils.searchEngagementTelemetryType(result),
      "weather",
      "Result telemetry type should not be 'weather'"
    );
  }
}

async function assertPageLoad() {
  let loadPromise = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false
  );

  EventUtils.synthesizeKey("KEY_ArrowDown", { repeat: EXPECTED_RESULT_INDEX });
  EventUtils.synthesizeKey("KEY_Enter");

  info("Waiting for weather page to load");
  await loadPromise;

  Assert.equal(
    gBrowser.currentURI.spec,
    "https://example.com/weather",
    "Expected weather page should have loaded"
  );

  await PlacesUtils.history.clear();
}
