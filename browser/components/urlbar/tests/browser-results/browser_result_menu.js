/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

add_setup(async function init() {
  registerCleanupFunction(async function () {
    Services.prefs.clearUserPref(
      "browser.urlbar.resultMenu.keyboardAccessible"
    );
    await PlacesUtils.history.clear();
    await UrlbarTestUtils.formHistory.clear();
  });
});

add_task(async function test_history() {
  const TEST_URL = "https://remove.me/from_urlbar/";
  await PlacesTestUtils.addVisits(TEST_URL);

  const resultIndex = 1;
  let result;
  let startQuery = async () => {
    await UrlbarTestUtils.promisePopupClose(window);
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "from_urlbar",
    });
    result = await UrlbarTestUtils.getDetailsOfResultAt(window, resultIndex);
    Assert.equal(result.url, TEST_URL, "Found the expected result");
    gURLBar.view.selectedRowIndex = resultIndex;
  };

  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.resultMenu.keyboardAccessible", false]],
  });
  await startQuery();
  EventUtils.synthesizeKey("KEY_Tab");
  isnot(
    UrlbarTestUtils.getSelectedElement(window),
    UrlbarTestUtils.getButtonForResultIndex(window, "result-menu", resultIndex),
    "Tab key skips over menu button with resultMenu.keyboardAccessible pref set to false"
  );
  info(
    "Checking that the mouse can still activate the menu button with resultMenu.keyboardAccessible = false"
  );
  await UrlbarTestUtils.openResultMenu(window, {
    byMouse: true,
    resultIndex,
  });
  gURLBar.view.resultMenu.removeAttribute("open");
  await SpecialPowers.popPrefEnv();
  await startQuery();
  EventUtils.synthesizeKey("KEY_Tab");
  is(
    UrlbarTestUtils.getSelectedElement(window),
    UrlbarTestUtils.getButtonForResultIndex(window, "result-menu", resultIndex),
    "Tab key doesn't skip over menu button with resultMenu.keyboardAccessible pref reset to true"
  );

  info("Checking that Space activates the menu button");
  await startQuery();
  await UrlbarTestUtils.openResultMenu(window, {
    activationKey: " ",
  });

  Assert.ok(
    (await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1)).hasAttribute(
      "menu-trigger"
    ),
    "Row is highlighted while open"
  );

  gURLBar.view.resultMenu.removeAttribute("open");

  info("Selecting Learn more item from the result menu");
  let tabOpenPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    Services.urlFormatter.formatURLPref("app.support.baseURL") +
      "awesome-bar-result-menu"
  );

  await UrlbarTestUtils.openResultMenuAndPressAccesskey(window, "L");
  info("Waiting for Learn more link to open in a new tab");
  await tabOpenPromise;
  gBrowser.removeCurrentTab();

  info("Restarting query in order to remove history entry via the menu");
  await startQuery();
  let promiseVisitRemoved = PlacesTestUtils.waitForNotification(
    "page-removed",
    events => events[0].url === TEST_URL
  );
  let expectedResultCount = UrlbarTestUtils.getResultCount(window) - 1;

  await UrlbarTestUtils.openResultMenuAndPressAccesskey(window, "R");
  const removeEvents = await promiseVisitRemoved;
  Assert.ok(
    removeEvents[0].isRemovedFromStore,
    "isRemovedFromStore should be true"
  );
  await TestUtils.waitForCondition(
    () => UrlbarTestUtils.getResultCount(window) == expectedResultCount,
    "Waiting for the result to disappear"
  );
  for (let i = 0; i < expectedResultCount; i++) {
    let details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    Assert.notEqual(
      details.url,
      TEST_URL,
      "Should not find the test URL in the remaining results"
    );
  }

  await UrlbarTestUtils.promisePopupClose(window);
  await PlacesUtils.history.clear();
});

add_task(async function test_remove_search_history() {
  let extension = await SearchTestUtils.installSearchExtension(
    {},
    {
      setAsDefault: true,
      skipUnload: true,
    }
  );
  let engine = SearchService.getEngineByName("Example");
  await SearchService.moveEngine(engine, 0);
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", true],
      ["browser.urlbar.maxHistoricalSearchSuggestions", 1],
    ],
  });

  let formHistoryValue = "foobar";
  await UrlbarTestUtils.formHistory.add([formHistoryValue]);

  let formHistory = (
    await UrlbarTestUtils.formHistory.search({
      value: formHistoryValue,
    })
  ).map(entry => entry.value);
  Assert.deepEqual(
    formHistory,
    [formHistoryValue],
    "Should find form history after adding it"
  );

  let promiseRemoved = UrlbarTestUtils.formHistory.promiseChanged("remove");

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "foo",
  });

  let resultIndex = 1;
  let count = UrlbarTestUtils.getResultCount(window);
  for (; resultIndex < count; resultIndex++) {
    let result = await UrlbarTestUtils.getDetailsOfResultAt(
      window,
      resultIndex
    );
    if (
      result.type == UrlbarShared.RESULT_TYPE.SEARCH &&
      result.source == UrlbarShared.RESULT_SOURCE.HISTORY
    ) {
      break;
    }
  }
  Assert.less(resultIndex, count, "Result found");

  await UrlbarTestUtils.openResultMenuAndPressAccesskey(window, "R", {
    resultIndex,
  });
  await promiseRemoved;

  await TestUtils.waitForCondition(
    () => UrlbarTestUtils.getResultCount(window) == count - 1,
    "Waiting for the result to disappear"
  );

  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    let result = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    Assert.ok(
      result.type != UrlbarShared.RESULT_TYPE.SEARCH ||
        result.source != UrlbarShared.RESULT_SOURCE.HISTORY,
      "Should not find the form history result in the remaining results"
    );
  }

  await UrlbarTestUtils.promisePopupClose(window);

  formHistory = (
    await UrlbarTestUtils.formHistory.search({
      value: formHistoryValue,
    })
  ).map(entry => entry.value);
  Assert.deepEqual(
    formHistory,
    [],
    "Should not find form history after removing it"
  );

  await SpecialPowers.popPrefEnv();
  await extension.unload();
});

add_task(async function firefoxSuggest() {
  const url = "https://example.com/hey-there";
  const helpUrl = "https://example.com/help";
  let provider = new UrlbarTestUtils.TestProvider({
    priority: Infinity,
    results: [
      new UrlbarResult({
        type: UrlbarShared.RESULT_TYPE.URL,
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        payload: {
          url,
          isBlockable: true,
          helpUrl,
          helpL10n: {
            id: "urlbar-result-menu-learn-more2",
          },
        },
      }),
    ],
  });

  // Implement the provider's `onEngagement()` so it removes the result.
  let onEngagementCallCount = 0;
  provider.onEngagement = (queryContext, controller, details) => {
    onEngagementCallCount++;
    controller.removeResult(details.result);
  };

  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  providersManager.registerProvider(provider);

  async function openResults() {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "test",
    });

    Assert.equal(
      UrlbarTestUtils.getResultCount(window),
      1,
      "There should be one result"
    );

    let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 0);
    Assert.equal(
      row.result.payload.url,
      url,
      "The result should be in the first row"
    );
  }

  await openResults();
  let tabOpenPromise = BrowserTestUtils.waitForNewTab(gBrowser, helpUrl);
  await UrlbarTestUtils.openResultMenuAndPressAccesskey(window, "L", {
    resultIndex: 0,
  });
  info("Waiting for help URL to load in a new tab");
  await tabOpenPromise;
  gBrowser.removeCurrentTab();

  await openResults();
  let engaged = UrlbarTestUtils.promiseProviderEngagement(window);
  await UrlbarTestUtils.openResultMenuAndPressAccesskey(window, "D", {
    resultIndex: 0,
  });
  await engaged;

  Assert.greater(
    onEngagementCallCount,
    0,
    "onEngagement() should have been called"
  );
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    0,
    "There should be no results after blocking"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  providersManager.unregisterProvider(provider);
});

add_task(async function test_toggle_keyboard_accessible() {
  const TEST_URL = "https://example.com/toggle_keyboard_accessible/";
  await PlacesTestUtils.addVisits(TEST_URL);

  const resultIndex = 1;
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "toggle_keyboard_accessible",
  });
  let result = await UrlbarTestUtils.getDetailsOfResultAt(window, resultIndex);
  Assert.equal(result.url, TEST_URL, "Found the expected result");

  let menuButton = UrlbarTestUtils.getButtonForResultIndex(
    window,
    "result-menu",
    resultIndex
  );

  let getItem = async ({ openByMouse = false } = {}) => {
    let item = await UrlbarTestUtils.openResultMenuAndGetItem({
      window,
      command: "toggle-keyboard-accessible",
      resultIndex,
      openByMouse,
    });
    Assert.equal(
      item,
      gURLBar.view.resultMenu.lastElementChild,
      "The checkbox is the last item in the menu"
    );
    Assert.equal(
      item.previousElementSibling.localName,
      "hr",
      "A separator sets the checkbox apart from the commands above it"
    );
    return item;
  };

  let item = await getItem();
  Assert.ok(
    !item.hasAttribute("checked"),
    "The checkbox is unchecked while the menu button is keyboard-accessible"
  );

  info("Unchecking the pref through the checkbox");
  let hidden = BrowserTestUtils.waitForEvent(gURLBar.view.resultMenu, "hidden");
  item.click();
  await hidden;
  Assert.ok(
    !UrlbarPrefs.get("resultMenu.keyboardAccessible"),
    "Picking the checkbox turned the pref off"
  );
  Assert.ok(
    menuButton.hasAttribute("keyboard-inaccessible"),
    "The already-built row's menu button became keyboard-inaccessible"
  );
  Assert.ok(gURLBar.view.isOpen, "The view stays open");

  item = await getItem({ openByMouse: true });
  Assert.ok(
    item.hasAttribute("checked"),
    "The checkbox is checked once the menu button is keyboard-inaccessible"
  );

  info("Restoring the pref through the checkbox");
  hidden = BrowserTestUtils.waitForEvent(gURLBar.view.resultMenu, "hidden");
  item.click();
  await hidden;
  Assert.ok(
    UrlbarPrefs.get("resultMenu.keyboardAccessible"),
    "Picking the checkbox again turned the pref back on"
  );
  Assert.ok(
    !menuButton.hasAttribute("keyboard-inaccessible"),
    "The already-built row's menu button became keyboard-accessible again"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  Services.prefs.clearUserPref("browser.urlbar.resultMenu.keyboardAccessible");
  await PlacesUtils.history.clear();
});

add_task(async function test_keyboard_accessible_checkbox_visibility() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.contextMenu.featureGate", true]],
  });
  await PlacesTestUtils.addVisits("https://example.com/checkbox_visibility/");

  const TEST_CASES = [
    {
      keyboardAccessiblePref: true,
      simulateTabKeyToSelect: true,
      openBy: "enter",
      expected: true,
    },
    {
      keyboardAccessiblePref: true,
      simulateTabKeyToSelect: true,
      openBy: "mouse",
      expected: true,
    },
    {
      keyboardAccessiblePref: true,
      simulateTabKeyToSelect: false,
      openBy: "mouse",
      expected: false,
    },
    {
      keyboardAccessiblePref: true,
      simulateTabKeyToSelect: true,
      openBy: "contextmenu",
      expected: false,
    },
    {
      keyboardAccessiblePref: true,
      simulateTabKeyToSelect: false,
      openBy: "contextmenu",
      expected: false,
    },
    {
      keyboardAccessiblePref: false,
      simulateTabKeyToSelect: true,
      openBy: "mouse",
      expected: true,
    },
    {
      keyboardAccessiblePref: false,
      simulateTabKeyToSelect: false,
      openBy: "contextmenu",
      expected: false,
    },
  ];

  for (let {
    keyboardAccessiblePref,
    simulateTabKeyToSelect,
    openBy,
    expected,
  } of TEST_CASES) {
    info(
      `Test for ${JSON.stringify({ keyboardAccessiblePref, simulateTabKeyToSelect, openBy })}`
    );
    await SpecialPowers.pushPrefEnv({
      set: [
        [
          "browser.urlbar.resultMenu.keyboardAccessible",
          keyboardAccessiblePref,
        ],
      ],
    });
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "checkbox_visibility",
    });

    let resultIndex = 1;
    if (simulateTabKeyToSelect) {
      info("Select the menu button with the Tab key");
      while (UrlbarTestUtils.getSelectedRowIndex(window) != resultIndex) {
        EventUtils.synthesizeKey("KEY_ArrowDown");
      }
      EventUtils.synthesizeKey("KEY_Tab");
      Assert.equal(
        UrlbarTestUtils.getSelectedElement(window) ==
          UrlbarTestUtils.getButtonForResultIndex(
            window,
            "result-menu",
            resultIndex
          ),
        keyboardAccessiblePref,
        "Whether the Tab key selected the menu button"
      );
    }

    info("Open the context menu");
    if (openBy == "contextmenu") {
      await openContextMenu(resultIndex);
    } else {
      await UrlbarTestUtils.openResultMenu(window, {
        resultIndex,
        byMouse: openBy == "mouse",
      });
    }

    let menu = gURLBar.view.resultMenu;
    Assert.equal(
      !!menu.querySelector('[data-command="toggle-keyboard-accessible"]'),
      expected,
      "Whether the menu shows the checkbox"
    );

    menu.hide(undefined, { force: true });
    await UrlbarTestUtils.promisePopupClose(window);
    await SpecialPowers.popPrefEnv();
  }

  await SpecialPowers.popPrefEnv();
  await PlacesUtils.history.clear();
});

async function openContextMenu(resultIndex) {
  let { element } = await UrlbarTestUtils.getDetailsOfResultAt(
    window,
    resultIndex
  );

  let onShown = BrowserTestUtils.waitForEvent(gURLBar.view.resultMenu, "shown");
  EventUtils.synthesizeMouseAtCenter(element.row, {
    button: 2,
    type: "contextmenu",
  });
  await onShown;
}
