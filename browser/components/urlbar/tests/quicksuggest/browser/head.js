/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let sandbox;

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser/head-common.js",
  this
);

ChromeUtils.defineESModuleGetters(this, {
  CONTEXTUAL_SERVICES_PING_TYPES:
    "resource:///modules/PartnerLinkAttribution.sys.mjs",
  QuickSuggest: "resource:///modules/QuickSuggest.sys.mjs",
  TelemetryTestUtils: "resource://testing-common/TelemetryTestUtils.sys.mjs",
  UrlbarProviderQuickSuggest:
    "resource:///modules/UrlbarProviderQuickSuggest.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "QuickSuggestTestUtils", () => {
  const { QuickSuggestTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/QuickSuggestTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

ChromeUtils.defineLazyGetter(this, "MerinoTestUtils", () => {
  const { MerinoTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/MerinoTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

ChromeUtils.defineLazyGetter(this, "PlacesFrecencyRecalculator", () => {
  return Cc["@mozilla.org/places/frecency-recalculator;1"].getService(
    Ci.nsIObserver
  ).wrappedJSObject;
});

registerCleanupFunction(async () => {
  // Ensure the popup is always closed at the end of each test to avoid
  // interfering with the next test.
  await UrlbarTestUtils.promisePopupClose(window);
});

/**
 * Updates the Top Sites feed.
 *
 * @param {Function} condition
 *   A callback that returns true after Top Sites are successfully updated.
 * @param {boolean} searchShortcuts
 *   True if Top Sites search shortcuts should be enabled.
 */
async function updateTopSites(condition, searchShortcuts = false) {
  // Toggle the pref to clear the feed cache and force an update.
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.newtabpage.activity-stream.discoverystream.endpointSpocsClear",
        "",
      ],
      ["browser.newtabpage.activity-stream.feeds.system.topsites", false],
      ["browser.newtabpage.activity-stream.feeds.system.topsites", true],
      [
        "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts",
        searchShortcuts,
      ],
    ],
  });

  // Wait for the feed to be updated.
  await TestUtils.waitForCondition(() => {
    let sites = AboutNewTab.getTopSites();
    return condition(sites);
  }, "Waiting for top sites to be updated");
}

/**
 * Call this in your setup task if you use `doQuickSuggestPingTest()`.
 *
 * @param {object} options
 *   Options
 * @param {Array} options.remoteSettingsRecords
 *   See `QuickSuggestTestUtils.ensureQuickSuggestInit()`.
 * @param {Array} options.merinoSuggestions
 *   See `QuickSuggestTestUtils.ensureQuickSuggestInit()`.
 * @param {Array} options.config
 *   See `QuickSuggestTestUtils.ensureQuickSuggestInit()`.
 */
async function initQuickSuggestPingTest({
  remoteSettingsRecords,
  merinoSuggestions = null,
  config = QuickSuggestTestUtils.DEFAULT_CONFIG,
}) {
  await SpecialPowers.pushPrefEnv({
    set: [
      // Switch-to-tab results can sometimes appear after the test clicks a help
      // button and closes the new tab, which interferes with the expected
      // indexes of quick suggest results, so disable them.
      ["browser.urlbar.suggest.openpage", false],
      ["browser.urlbar.quicksuggest.ampTopPickCharThreshold", 0],
    ],
  });

  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
  await UrlbarTestUtils.formHistory.clear();

  // Add a mock engine so we don't hit the network.
  await SearchTestUtils.installSearchExtension({}, { setAsDefault: true });

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords,
    merinoSuggestions,
    config,
  });
}

/**
 * Main entry point for testing the `quick-suggest` ping. It tests impressions,
 * clicks, and commands.
 *
 * @param {object} options
 *   Options
 * @param {number} options.index
 *   The expected index of the suggestion in the results list.
 * @param {object} options.suggestion
 *   The suggestion being tested.
 * @param {object} options.impressionOnly
 *   The expected impression ping when only an impression is triggered for the
 *   suggestion (no engagement or dismissal). It should be an object that maps
 *   the camelCased ping keys to their expected values.
 * @param {Array} options.click
 *   An array of expected pings (in the order they're expected) when the
 *   suggestion is clicked.
 * @param {Array} options.commands
 *   Each element in this array is an object that describes the expected pings
 *   for a result menu command. Each object must have the following properties:
 *     {string|Array} command
 *       A command name or array; this is passed directly to
 *       `UrlbarTestUtils.openResultMenuAndClickItem()` as the `commandOrArray`
 *       arg, so see its documentation for details.
 *     {Array} pings
 *       An array of expected pings (in the order they're expected) when the
 *       command is triggered.
 * @param {string} options.providerName
 *   The name of the provider that is expected to create the UrlbarResult for
 *   the suggestion.
 * @param {Function} options.teardown
 *   If given, this function will be called after each selectable test. If
 *   picking an element causes side effects that need to be cleaned up before
 *   starting the next selectable test, they can be cleaned up here.
 * @param {Function} options.showSuggestion
 *   This function should open the view and show the suggestion.
 */
async function doQuickSuggestPingTest({
  index,
  suggestion,
  impressionOnly,
  click,
  commands,
  providerName = UrlbarProviderQuickSuggest.name,
  teardown = null,
  showSuggestion = () =>
    UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      // If the suggestion is a mock remote settings suggestion, it will have a
      // `keywords` property. Otherwise the suggestion object must be a Merino
      // suggestion, and the search string doesn't matter in that case because
      // the mock Merino server will be set up to return suggestions regardless.
      value: suggestion.keywords?.[0] || "test",
      fireInputEvent: true,
    }),
}) {
  await doImpressionOnlyTest({
    index,
    suggestion,
    providerName,
    showSuggestion,
    expectedPing: impressionOnly,
  });

  await doClickTest({
    suggestion,
    providerName,
    showSuggestion,
    index,
    expectedPings: click,
  });

  for (let command of commands) {
    await doCommandTest({
      suggestion,
      providerName,
      showSuggestion,
      index,
      commandOrArray: command.command,
      expectedPings: command.pings,
    });

    if (teardown) {
      info("Calling teardown");
      await teardown();
      info("Finished teardown");
    }
  }
}

/**
 * Helper for `doQuickSuggestPingTest()` that does an impression-only test.
 *
 * @param {object} options
 *   Options
 * @param {number} options.index
 *   The expected index of the suggestion in the results list.
 * @param {object} options.suggestion
 *   The suggestion being tested.
 * @param {string} options.providerName
 *   The name of the provider that is expected to create the UrlbarResult for
 *   the suggestion.
 * @param {object} options.expectedPing
 *   The expected impression ping.
 * @param {Function} options.showSuggestion
 *   This function should open the view and show the suggestion.
 */
async function doImpressionOnlyTest({
  index,
  suggestion,
  providerName,
  expectedPing,
  showSuggestion,
}) {
  info("Starting impression-only test");

  let expectedPings = [expectedPing];
  let gleanPingCount = watchQuickSuggestPings(expectedPings);

  info("Showing suggestion");
  await showSuggestion();

  // Get the suggestion row.
  let row = await validateSuggestionRow(index, suggestion, providerName);
  if (!row) {
    Assert.ok(
      false,
      "Couldn't get suggestion row, stopping impression-only test"
    );
    return;
  }

  // We need to get a different selectable row so we can pick it to trigger
  // impression-only telemetry. For simplicity we'll look for a row that will
  // load a URL when picked. We'll also verify no other rows are from the
  // expected provider.
  let otherRow;
  let rowCount = UrlbarTestUtils.getResultCount(window);
  for (let i = 0; i < rowCount; i++) {
    if (i != index) {
      let r = await UrlbarTestUtils.waitForAutocompleteResultAt(window, i);
      Assert.notEqual(
        r.result.providerName,
        providerName,
        "No other row should be from expected provider: index = " + i
      );
      if (
        !otherRow &&
        (r.result.payload.url ||
          (r.result.type == UrlbarUtils.RESULT_TYPE.SEARCH &&
            (r.result.payload.query || r.result.payload.suggestion))) &&
        r.hasAttribute("row-selectable")
      ) {
        otherRow = r;
      }
    }
  }
  if (!otherRow) {
    Assert.ok(
      false,
      "Couldn't get a different selectable row with a URL, stopping impression-only test"
    );
    return;
  }

  // Pick the different row. Assumptions:
  // * The middle of the row is selectable
  // * Picking the row will load a page
  info("Clicking different row and waiting for view to close");
  let loadPromise = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  await UrlbarTestUtils.promisePopupClose(window, () =>
    EventUtils.synthesizeMouseAtCenter(otherRow, {})
  );

  info("Waiting for page to load after clicking different row");
  await loadPromise;

  Assert.equal(
    expectedPings.length,
    gleanPingCount.value,
    "Submitted one Glean ping per expected ping"
  );

  // Clean up.
  await PlacesUtils.history.clear();
  await UrlbarTestUtils.formHistory.clear();

  info("Finished impression-only test");
}

/**
 * Helper for `doQuickSuggestPingTest()` that clicks a suggestion's row.
 *
 * @param {object} options
 *   Options
 * @param {number} options.index
 *   The expected index of the suggestion in the results list.
 * @param {object} options.suggestion
 *   The suggestion being tested.
 * @param {string} options.providerName
 *   The name of the provider that is expected to create the UrlbarResult for
 *   the suggestion.
 * @param {object} options.expectedPings
 *   An array of expected pings (in the order they're expected).
 * @param {Function} options.showSuggestion
 *   This function should open the view and show the suggestion.
 */
async function doClickTest({
  index,
  suggestion,
  providerName,
  expectedPings,
  showSuggestion,
}) {
  info("Starting click test");

  let gleanPingCount = watchQuickSuggestPings(expectedPings);

  info("Showing suggestion");
  await showSuggestion();

  let row = await validateSuggestionRow(index, suggestion, providerName);
  if (!row) {
    Assert.ok(false, "Couldn't get suggestion row, stopping click test");
    return;
  }

  // We assume clicking the row will load a page in the current browser.
  let loadPromise = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);

  info("Clicking row");
  EventUtils.synthesizeMouseAtCenter(row, {});

  info("Waiting for load");
  await loadPromise;
  await TestUtils.waitForTick();

  Assert.equal(
    expectedPings.length,
    gleanPingCount.value,
    "Submitted one Glean ping per expected ping"
  );

  await PlacesUtils.history.clear();

  info("Finished click test");
}

/**
 * Helper for `doQuickSuggestPingTest()` that clicks a result menu command.
 *
 * @param {object} options
 *   Options
 * @param {number} options.index
 *   The expected index of the suggestion in the results list.
 * @param {object} options.suggestion
 *   The suggestion being tested.
 * @param {string} options.providerName
 *   The name of the provider that is expected to create the UrlbarResult for
 *   the suggestion.
 * @param {string|Array} options.commandOrArray
 *   A command name or array; this is passed directly to
 *  `UrlbarTestUtils.openResultMenuAndClickItem()` as the `commandOrArray` arg,
 *   so see its documentation for details.
 * @param {object} options.expectedPings
 *   An array of expected pings (in the order they're expected).
 * @param {Function} options.showSuggestion
 *   This function should open the view and show the suggestion.
 */
async function doCommandTest({
  index,
  suggestion,
  providerName,
  commandOrArray,
  expectedPings,
  showSuggestion,
}) {
  info("Starting command test: " + JSON.stringify({ commandOrArray }));

  let gleanPingCount = watchQuickSuggestPings(expectedPings);

  info("Showing suggestion");
  await showSuggestion();

  let row = await validateSuggestionRow(index, suggestion, providerName);
  if (!row) {
    Assert.ok(false, "Couldn't get suggestion row, stopping click test");
    return;
  }

  let command =
    typeof commandOrArray == "string"
      ? commandOrArray
      : commandOrArray[commandOrArray.length - 1];

  let loadPromise;
  if (command == "help" || command == "manage") {
    // We assume clicking this command will load a page in a new tab.
    loadPromise = BrowserTestUtils.waitForNewTab(gBrowser);
  }

  info("Clicking command");
  await UrlbarTestUtils.openResultMenuAndClickItem(window, commandOrArray, {
    resultIndex: index,
    openByMouse: true,
  });

  if (loadPromise) {
    info("Waiting for load");
    await loadPromise;
    await TestUtils.waitForTick();
    if (command == "help" || command == "manage") {
      info("Closing help or manage tab");
      BrowserTestUtils.removeTab(gBrowser.selectedTab);
    }
  }

  Assert.equal(
    expectedPings.length,
    gleanPingCount.value,
    "Submitted one Glean ping per expected ping"
  );

  if (command == "dismiss") {
    await QuickSuggest.clearDismissedSuggestions();
  }
  await PlacesUtils.history.clear();

  info("Finished command test: " + JSON.stringify({ commandOrArray }));
}

/**
 * Do test the "Manage" result menu item.
 *
 * @param {object} options
 *   Options
 * @param {number} options.index
 *   The index of the suggestion that will be checked in the results list.
 * @param {number} options.input
 *   The input value on the urlbar.
 */
async function doManageTest({ index, input }) {
  await BrowserTestUtils.withNewTab({ gBrowser }, async browser => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: input,
    });

    const managePage = "about:preferences#search";
    let onManagePageLoaded = BrowserTestUtils.browserLoaded(
      browser,
      false,
      managePage
    );
    // Click the command.
    await UrlbarTestUtils.openResultMenuAndClickItem(window, "manage", {
      resultIndex: index,
    });
    await onManagePageLoaded;

    Assert.equal(
      browser.currentURI.spec,
      managePage,
      "The manage page is loaded"
    );

    await UrlbarTestUtils.promisePopupClose(window);
  });
}

/**
 * Gets a row in the view, which is assumed to be open, and asserts that it's a
 * particular quick suggest row. If it is, the row is returned. If it's not,
 * null is returned.
 *
 * @param {number} index
 *   The expected index of the quick suggest row.
 * @param {object} suggestion
 *   The expected suggestion.
 * @param {string} providerName
 *   The name of the provider that is expected to create the UrlbarResult for
 *   the suggestion.
 * @returns {Element}
 *   If the row is the expected suggestion, the row element is returned.
 *   Otherwise null is returned.
 */
async function validateSuggestionRow(index, suggestion, providerName) {
  let rowCount = UrlbarTestUtils.getResultCount(window);
  Assert.less(
    index,
    rowCount,
    "Expected suggestion row index should be < row count"
  );
  if (rowCount <= index) {
    return null;
  }

  let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, index);
  Assert.equal(
    row.result.providerName,
    providerName,
    "Expected suggestion row should be from expected provider"
  );
  Assert.equal(
    row.result.payload.url,
    suggestion.url,
    "The suggestion row should represent the expected suggestion"
  );
  if (
    row.result.providerName != providerName ||
    row.result.payload.url != suggestion.url
  ) {
    return null;
  }

  return row;
}

function watchQuickSuggestPings(pings) {
  let countObject = { value: 0 };

  let checkPing = (ping, next) => {
    countObject.value++;
    assertQuickSuggestPing(ping);
    if (next) {
      GleanPings.quickSuggest.testBeforeNextSubmit(next);
    }
  };

  // Build the chain of `testBeforeNextSubmit`s backwards.
  let next = undefined;
  pings
    .slice()
    .reverse()
    .forEach(ping => {
      next = checkPing.bind(null, ping, next);
    });
  if (next) {
    GleanPings.quickSuggest.testBeforeNextSubmit(next);
  }

  return countObject;
}

function assertQuickSuggestPing(expectedPing) {
  let expectedKeys = [
    "pingType",
    "matchType",
    "advertiser",
    "blockId",
    "improveSuggestExperience",
    "position",
    "suggestedIndex",
    "suggestedIndexRelativeToGroup",
    "requestId",
    "source",
    "contextId",
  ];

  Assert.ok(
    expectedPing.pingType,
    "Sanity check: The expected ping should have a 'pingType'"
  );
  switch (expectedPing.pingType) {
    case CONTEXTUAL_SERVICES_PING_TYPES.QS_IMPRESSION:
      expectedKeys.push("isClicked", "reportingUrl");
      break;
    case CONTEXTUAL_SERVICES_PING_TYPES.QS_SELECTION:
      expectedKeys.push("reportingUrl");
      break;
    case CONTEXTUAL_SERVICES_PING_TYPES.QS_BLOCK:
      expectedKeys.push("iabCategory");
      break;
  }

  let expectedValueOverrides = {
    contextId: expectedPingContextId(),
  };

  for (let key of expectedKeys) {
    Assert.ok(
      expectedPing.hasOwnProperty(key),
      "Sanity check: The expected ping should have key: " + key
    );
    Assert.ok(
      key in Glean.quickSuggest,
      "The actual ping should have key: " + key
    );

    let expectedValue = expectedValueOverrides.hasOwnProperty(key)
      ? expectedValueOverrides[key]
      : expectedPing[key];

    if (expectedValue === undefined || expectedValue === "") {
      // The value is specifically not set in this case, which ends up recording
      // a null value in the actual ping.
      Assert.strictEqual(
        Glean.quickSuggest[key].testGetValue(),
        null,
        "The actual ping should have a null value for key: " + key
      );
    } else {
      Assert.strictEqual(
        Glean.quickSuggest[key].testGetValue(),
        expectedValue,
        "The actual ping should have the correct value for key: " + key
      );
    }
  }
}

function expectedPingContextId() {
  // `contextId` in the `quick-suggest` pings should always be the value in this
  // pref, a UUID, but without the leading and trailing braces.
  return Services.prefs
    .getCharPref("browser.contextual-services.contextId")
    .substring(1, 37);
}
