/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const TEST_PAGE_BASENAME = "contentSearchUI.html";

const TEST_ENGINE1 = {
  name: "searchSuggestionEngine1",
  id: "other-searchSuggestionEngine1",
  loadPath: "[addon]searchsuggestionengine1@tests.mozilla.org",
};
const TEST_ENGINE2 = {
  name: "searchSuggestionEngine2",
  id: "other-searchSuggestionEngine2",
  loadPath: "[addon]searchsuggestionengine2@tests.mozilla.org",
};

const TEST_MSG = "ContentSearchUIControllerTest";

ChromeUtils.defineESModuleGetters(this, {
  ContentSearch: "resource:///actors/ContentSearchParent.sys.mjs",
  FormHistoryTestUtils:
    "resource://testing-common/FormHistoryTestUtils.sys.mjs",
  SearchSuggestionController:
    "resource://gre/modules/SearchSuggestionController.sys.mjs",
});

requestLongerTimeout(2);

function waitForSuggestions() {
  return SpecialPowers.spawn(gBrowser.selectedBrowser, [], () =>
    ContentTaskUtils.waitForCondition(
      () =>
        Cu.waiveXrays(content).gController.input.getAttribute(
          "aria-expanded"
        ) == "true",
      "Waiting for suggestions",
      200 // Increased interval to support long textruns.
    )
  );
}

async function waitForSearch() {
  await BrowserTestUtils.waitForContentEvent(
    gBrowser.selectedBrowser,
    "ContentSearchClient",
    true,
    event => {
      if (event.detail.type == "Search") {
        event.target._eventDetail = event.detail.data;
        return true;
      }
      return false;
    },
    true
  );
  return SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    let eventDetail = content._eventDetail;
    delete content._eventDetail;
    return eventDetail;
  });
}

async function waitForSearchSettings() {
  await BrowserTestUtils.waitForContentEvent(
    gBrowser.selectedBrowser,
    "ContentSearchClient",
    true,
    event => {
      if (event.detail.type == "ManageEngines") {
        event.target._eventDetail = event.detail.data;
        return true;
      }
      return false;
    },
    true
  );
  return SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    let eventDetail = content._eventDetail;
    delete content._eventDetail;
    return eventDetail;
  });
}

function getCurrentState() {
  return SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    let controller = Cu.waiveXrays(content).gController;
    let state = {
      selectedIndex: controller.selectedIndex,
      selectedButtonIndex: controller.selectedButtonIndex,
      numSuggestions: controller._table.hidden ? 0 : controller.numSuggestions,
      suggestionAtIndex: [],
      isFormHistorySuggestionAtIndex: [],

      tableHidden: controller._table.hidden,

      inputValue: controller.input.value,
      ariaExpanded: controller.input.getAttribute("aria-expanded"),
    };

    if (state.numSuggestions) {
      for (let i = 0; i < controller.numSuggestions; i++) {
        state.suggestionAtIndex.push(controller.suggestionAtIndex(i));
        state.isFormHistorySuggestionAtIndex.push(
          controller.isFormHistorySuggestionAtIndex(i)
        );
      }
    }

    return state;
  });
}

async function msg(type, data = null) {
  switch (type) {
    case "reset":
      // Reset both the input and suggestions by select all + delete. If there was
      // no text entered, this won't have any effect, so also escape to ensure the
      // suggestions table is closed.
      await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
        Cu.waiveXrays(content).gController.input.focus();
        EventUtils.synthesizeKey("a", { accelKey: true }, content);
        EventUtils.synthesizeKey("KEY_Delete", {}, content);
        EventUtils.synthesizeKey("KEY_Escape", {}, content);
      });
      break;

    case "key": {
      let keyName = typeof data == "string" ? data : data.key;
      await BrowserTestUtils.synthesizeKey(
        keyName,
        data.modifiers || {},
        gBrowser.selectedBrowser
      );
      if (data?.waitForSuggestions) {
        await waitForSuggestions();
      }
      break;
    }
    case "text": {
      await SpecialPowers.spawn(
        gBrowser.selectedBrowser,
        [data.value],
        text => {
          Cu.waiveXrays(content).gController.input.value = text.substring(
            0,
            text.length - 1
          );
          EventUtils.synthesizeKey(
            text.substring(text.length - 1),
            {},
            content
          );
        }
      );
      if (data?.waitForSuggestions) {
        await waitForSuggestions();
      }
      break;
    }
    case "startComposition":
      await BrowserTestUtils.synthesizeComposition(
        "compositionstart",
        gBrowser.selectedBrowser
      );
      break;
    case "changeComposition": {
      await BrowserTestUtils.synthesizeCompositionChange(
        {
          composition: {
            string: data.data,
            clauses: [
              {
                length: data.length,
                attr: Ci.nsITextInputProcessor.ATTR_RAW_CLAUSE,
              },
            ],
          },
          caret: { start: data.length, length: 0 },
        },
        gBrowser.selectedBrowser
      );
      if (data?.waitForSuggestions) {
        await waitForSuggestions();
      }
      break;
    }
    case "commitComposition":
      await BrowserTestUtils.synthesizeComposition(
        "compositioncommitasis",
        gBrowser.selectedBrowser
      );
      break;
    case "mousemove":
    case "click": {
      let event;
      let index;
      if (type == "mousemove") {
        event = {
          type: "mousemove",
          clickcount: 0,
        };
        index = data;
      } else {
        event = data.modifiers || null;
        index = data.eltIdx;
      }

      await SpecialPowers.spawn(
        gBrowser.selectedBrowser,
        [type, event, index],
        (eventType, eventArgs, itemIndex) => {
          let controller = Cu.waiveXrays(content).gController;
          return new Promise(resolve => {
            let row;
            if (itemIndex == -1) {
              row = controller._table.firstChild;
            } else {
              let allElts = [
                ...controller._suggestionsList.children,
                ...controller._oneOffButtons,
                content.document.getElementById("contentSearchSettingsButton"),
              ];
              row = allElts[itemIndex];
            }
            row.addEventListener(eventType, () => resolve(), { once: true });
            EventUtils.synthesizeMouseAtCenter(row, eventArgs, content);
          });
        }
      );
      break;
    }
  }

  return getCurrentState();
}

/**
 * Focusses the in-content search bar.
 *
 * @returns {Promise}
 *   A promise that is resolved once the focus is complete.
 */
function focusContentSearchBar() {
  return SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    Cu.waiveXrays(content).input.focus();
  });
}

let extension1;
let extension2;

add_setup(async function () {
  const pageURL = getRootDirectory(gTestPath) + TEST_PAGE_BASENAME;

  let cleanupAboutPage;
  await BrowserTestUtils.registerAboutPage(
    callback => (cleanupAboutPage = callback),
    "test-about-content-search-ui",
    pageURL,
    Ci.nsIAboutModule.URI_SAFE_FOR_UNTRUSTED_CONTENT |
      Ci.nsIAboutModule.URI_MUST_LOAD_IN_CHILD |
      Ci.nsIAboutModule.ALLOW_SCRIPT |
      Ci.nsIAboutModule.URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS
  );

  let originalOnMessageSearch = ContentSearch._onMessageSearch;
  let originalOnMessageManageEngines = ContentSearch._onMessageManageEngines;

  ContentSearch._onMessageSearch = () => {};
  ContentSearch._onMessageManageEngines = () => {};

  let currentEngines = await Services.search.getVisibleEngines();

  extension1 = await SearchTestUtils.installSearchExtension(
    {
      name: TEST_ENGINE1.name,
      suggest_url:
        "https://example.com/browser/browser/components/search/test/browser/searchSuggestionEngine.sjs",
      suggest_url_get_params: "query={searchTerms}",
    },
    { setAsDefault: true }
  );
  extension2 = await SearchTestUtils.installSearchExtension({
    name: TEST_ENGINE2.name,
    suggest_url:
      "https://example.com/browser/browser/components/search/test/browser/searchSuggestionEngine.sjs",
    suggest_url_get_params: "query={searchTerms}",
  });

  for (let engine of currentEngines) {
    await Services.search.removeEngine(engine);
  }

  registerCleanupFunction(async () => {
    // Ensure tabs are closed before we continue on with the cleanup.
    for (let tab of tabs) {
      BrowserTestUtils.removeTab(tab);
    }
    Services.search.restoreDefaultEngines();

    await TestUtils.waitForTick();

    ContentSearch._onMessageSearch = originalOnMessageSearch;
    ContentSearch._onMessageManageEngines = originalOnMessageManageEngines;

    if (cleanupAboutPage) {
      await cleanupAboutPage();
    }
  });

  await promiseTab();
});

add_task(async function emptyInput() {
  await focusContentSearchBar();

  let state = await msg("key", { key: "x", waitForSuggestions: true });
  checkState(state, "x", ["xfoo", "xbar"], -1);

  state = await msg("key", "VK_BACK_SPACE");
  checkState(state, "", [], -1);

  await msg("reset");
});

add_task(async function blur() {
  await focusContentSearchBar();

  let state = await msg("key", { key: "x", waitForSuggestions: true });
  checkState(state, "x", ["xfoo", "xbar"], -1);

  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    Cu.waiveXrays(content).gController.input.blur();
  });
  state = await getCurrentState();
  checkState(state, "x", [], -1);

  await msg("reset");
});

add_task(async function upDownKeys() {
  await focusContentSearchBar();

  let state = await msg("key", { key: "x", waitForSuggestions: true });
  checkState(state, "x", ["xfoo", "xbar"], -1);

  // Cycle down the suggestions starting from no selection.
  state = await msg("key", "VK_DOWN");
  checkState(state, "xfoo", ["xfoo", "xbar"], 0);

  state = await msg("key", "VK_DOWN");
  checkState(state, "xbar", ["xfoo", "xbar"], 1);

  state = await msg("key", "VK_DOWN");
  checkState(state, "x", ["xfoo", "xbar"], 2);

  state = await msg("key", "VK_DOWN");
  checkState(state, "x", ["xfoo", "xbar"], 3);

  state = await msg("key", "VK_DOWN");
  checkState(state, "x", ["xfoo", "xbar"], -1);

  // Cycle up starting from no selection.
  state = await msg("key", "VK_UP");
  checkState(state, "x", ["xfoo", "xbar"], 3);

  state = await msg("key", "VK_UP");
  checkState(state, "x", ["xfoo", "xbar"], 2);

  state = await msg("key", "VK_UP");
  checkState(state, "xbar", ["xfoo", "xbar"], 1);

  state = await msg("key", "VK_UP");
  checkState(state, "xfoo", ["xfoo", "xbar"], 0);

  state = await msg("key", "VK_UP");
  checkState(state, "x", ["xfoo", "xbar"], -1);

  await msg("reset");
});

add_task(async function rightLeftKeys() {
  await focusContentSearchBar();

  let state = await msg("key", { key: "x", waitForSuggestions: true });
  checkState(state, "x", ["xfoo", "xbar"], -1);

  state = await msg("key", "VK_LEFT");
  checkState(state, "x", ["xfoo", "xbar"], -1);

  state = await msg("key", "VK_LEFT");
  checkState(state, "x", ["xfoo", "xbar"], -1);

  state = await msg("key", "VK_RIGHT");
  checkState(state, "x", ["xfoo", "xbar"], -1);

  state = await msg("key", "VK_RIGHT");
  checkState(state, "x", [], -1);

  state = await msg("key", { key: "VK_DOWN", waitForSuggestions: true });
  checkState(state, "x", ["xfoo", "xbar"], -1);

  state = await msg("key", "VK_DOWN");
  checkState(state, "xfoo", ["xfoo", "xbar"], 0);

  // This should make the xfoo suggestion sticky.  To make sure it sticks,
  // trigger suggestions again and cycle through them by pressing Down until
  // nothing is selected again.
  state = await msg("key", "VK_RIGHT");
  checkState(state, "xfoo", [], -1);

  state = await msg("key", { key: "VK_DOWN", waitForSuggestions: true });
  checkState(state, "xfoo", ["xfoofoo", "xfoobar"], -1);

  state = await msg("key", "VK_DOWN");
  checkState(state, "xfoofoo", ["xfoofoo", "xfoobar"], 0);

  state = await msg("key", "VK_DOWN");
  checkState(state, "xfoobar", ["xfoofoo", "xfoobar"], 1);

  state = await msg("key", "VK_DOWN");
  checkState(state, "xfoo", ["xfoofoo", "xfoobar"], 2);

  state = await msg("key", "VK_DOWN");
  checkState(state, "xfoo", ["xfoofoo", "xfoobar"], 3);

  state = await msg("key", "VK_DOWN");
  checkState(state, "xfoo", ["xfoofoo", "xfoobar"], -1);

  await msg("reset");
});

add_task(async function tabKey() {
  await focusContentSearchBar();
  await msg("key", { key: "x", waitForSuggestions: true });

  let state = await msg("key", "VK_TAB");
  checkState(state, "x", ["xfoo", "xbar"], 2);

  state = await msg("key", "VK_TAB");
  checkState(state, "x", ["xfoo", "xbar"], 3);

  state = await msg("key", { key: "VK_TAB", modifiers: { shiftKey: true } });
  checkState(state, "x", ["xfoo", "xbar"], 2);

  state = await msg("key", { key: "VK_TAB", modifiers: { shiftKey: true } });
  checkState(state, "x", [], -1);

  await focusContentSearchBar();

  await msg("key", { key: "VK_DOWN", waitForSuggestions: true });

  for (let i = 0; i < 3; ++i) {
    state = await msg("key", "VK_TAB");
  }
  checkState(state, "x", [], -1);

  await focusContentSearchBar();

  await msg("key", { key: "VK_DOWN", waitForSuggestions: true });
  state = await msg("key", "VK_DOWN");
  checkState(state, "xfoo", ["xfoo", "xbar"], 0);

  state = await msg("key", "VK_TAB");
  checkState(state, "xfoo", ["xfoo", "xbar"], 0, 0);

  state = await msg("key", "VK_TAB");
  checkState(state, "xfoo", ["xfoo", "xbar"], 0, 1);

  state = await msg("key", "VK_DOWN");
  checkState(state, "xbar", ["xfoo", "xbar"], 1, 1);

  state = await msg("key", "VK_DOWN");
  checkState(state, "x", ["xfoo", "xbar"], 2);

  state = await msg("key", "VK_UP");
  checkState(state, "xbar", ["xfoo", "xbar"], 1);

  state = await msg("key", "VK_TAB");
  checkState(state, "xbar", ["xfoo", "xbar"], 1, 0);

  state = await msg("key", "VK_TAB");
  checkState(state, "xbar", ["xfoo", "xbar"], 1, 1);

  state = await msg("key", "VK_TAB");
  checkState(state, "xbar", [], -1);

  await msg("reset");
});

add_task(async function cycleSuggestions() {
  await focusContentSearchBar();
  await msg("key", { key: "x", waitForSuggestions: true });

  let cycle = async function (aSelectedButtonIndex) {
    let modifiers = {
      shiftKey: true,
      accelKey: true,
    };

    let state = await msg("key", { key: "VK_DOWN", modifiers });
    checkState(state, "xfoo", ["xfoo", "xbar"], 0, aSelectedButtonIndex);

    state = await msg("key", { key: "VK_DOWN", modifiers });
    checkState(state, "xbar", ["xfoo", "xbar"], 1, aSelectedButtonIndex);

    state = await msg("key", { key: "VK_DOWN", modifiers });
    checkState(state, "x", ["xfoo", "xbar"], -1, aSelectedButtonIndex);

    state = await msg("key", { key: "VK_DOWN", modifiers });
    checkState(state, "xfoo", ["xfoo", "xbar"], 0, aSelectedButtonIndex);

    state = await msg("key", { key: "VK_UP", modifiers });
    checkState(state, "x", ["xfoo", "xbar"], -1, aSelectedButtonIndex);

    state = await msg("key", { key: "VK_UP", modifiers });
    checkState(state, "xbar", ["xfoo", "xbar"], 1, aSelectedButtonIndex);

    state = await msg("key", { key: "VK_UP", modifiers });
    checkState(state, "xfoo", ["xfoo", "xbar"], 0, aSelectedButtonIndex);

    state = await msg("key", { key: "VK_UP", modifiers });
    checkState(state, "x", ["xfoo", "xbar"], -1, aSelectedButtonIndex);
  };

  await cycle();

  // Repeat with a one-off selected.
  let state = await msg("key", "VK_TAB");
  checkState(state, "x", ["xfoo", "xbar"], 2);
  await cycle(0);

  // Repeat with the settings button selected.
  state = await msg("key", "VK_TAB");
  checkState(state, "x", ["xfoo", "xbar"], 3);
  await cycle(1);

  await msg("reset");
});

add_task(async function cycleOneOffs() {
  await focusContentSearchBar();
  await msg("key", { key: "x", waitForSuggestions: true });

  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    let btn =
      Cu.waiveXrays(content).gController._oneOffButtons[
        Cu.waiveXrays(content).gController._oneOffButtons.length - 1
      ];
    let newBtn = btn.cloneNode(true);
    btn.parentNode.appendChild(newBtn);
    Cu.waiveXrays(content).gController._oneOffButtons.push(newBtn);
  });

  let state = await msg("key", "VK_DOWN");
  state = await msg("key", "VK_DOWN");
  checkState(state, "xbar", ["xfoo", "xbar"], 1);

  let modifiers = {
    altKey: true,
  };

  state = await msg("key", { key: "VK_DOWN", modifiers });
  checkState(state, "xbar", ["xfoo", "xbar"], 1, 0);

  state = await msg("key", { key: "VK_DOWN", modifiers });
  checkState(state, "xbar", ["xfoo", "xbar"], 1, 1);

  state = await msg("key", { key: "VK_DOWN", modifiers });
  checkState(state, "xbar", ["xfoo", "xbar"], 1);

  state = await msg("key", { key: "VK_UP", modifiers });
  checkState(state, "xbar", ["xfoo", "xbar"], 1, 1);

  state = await msg("key", { key: "VK_UP", modifiers });
  checkState(state, "xbar", ["xfoo", "xbar"], 1, 0);

  state = await msg("key", { key: "VK_UP", modifiers });
  checkState(state, "xbar", ["xfoo", "xbar"], 1);

  // If the settings button is selected, pressing alt+up/down should select the
  // last/first one-off respectively (and deselect the settings button).
  await msg("key", "VK_TAB");
  await msg("key", "VK_TAB");
  state = await msg("key", "VK_TAB"); // Settings button selected.
  checkState(state, "xbar", ["xfoo", "xbar"], 1, 2);

  state = await msg("key", { key: "VK_UP", modifiers });
  checkState(state, "xbar", ["xfoo", "xbar"], 1, 1);

  state = await msg("key", "VK_TAB");
  checkState(state, "xbar", ["xfoo", "xbar"], 1, 2);

  state = await msg("key", { key: "VK_DOWN", modifiers });
  checkState(state, "xbar", ["xfoo", "xbar"], 1, 0);

  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    Cu.waiveXrays(content).gController._oneOffButtons.pop().remove();
  });
  await msg("reset");
});

add_task(async function mouse() {
  await focusContentSearchBar();

  let state = await msg("key", { key: "x", waitForSuggestions: true });
  checkState(state, "x", ["xfoo", "xbar"], -1);

  state = await msg("mousemove", 0);
  checkState(state, "x", ["xfoo", "xbar"], 0);

  state = await msg("mousemove", 1);
  checkState(state, "x", ["xfoo", "xbar"], 1);

  state = await msg("mousemove", 2);
  checkState(state, "x", ["xfoo", "xbar"], 2, 0);

  state = await msg("mousemove", 3);
  checkState(state, "x", ["xfoo", "xbar"], 3, 1);

  state = await msg("mousemove", -1);
  checkState(state, "x", ["xfoo", "xbar"], -1);

  await msg("reset");
  await focusContentSearchBar();

  state = await msg("key", { key: "x", waitForSuggestions: true });
  checkState(state, "x", ["xfoo", "xbar"], -1);

  state = await msg("mousemove", 0);
  checkState(state, "x", ["xfoo", "xbar"], 0);

  state = await msg("mousemove", 2);
  checkState(state, "x", ["xfoo", "xbar"], 2, 0);

  state = await msg("mousemove", -1);
  checkState(state, "x", ["xfoo", "xbar"], -1);

  await msg("reset");
});

add_task(async function formHistory() {
  await focusContentSearchBar();

  // Type an X and add it to form history.
  let state = await msg("key", { key: "x", waitForSuggestions: true });
  checkState(state, "x", ["xfoo", "xbar"], -1);
  // Wait for Satchel to say it's been added to form history.
  let observePromise = new Promise(resolve => {
    Services.obs.addObserver(function onAdd(subj, topic, data) {
      if (data == "formhistory-add") {
        Services.obs.removeObserver(onAdd, "satchel-storage-changed");
        executeSoon(resolve);
      }
    }, "satchel-storage-changed");
  });

  await FormHistoryTestUtils.clear("searchbar-history");
  let entry = await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    return Cu.waiveXrays(content).gController.addInputValueToFormHistory();
  });
  await observePromise;
  Assert.greater(
    await FormHistoryTestUtils.count("searchbar-history", {
      source: entry.source,
    }),
    0
  );

  // Reset the input.
  state = await msg("reset");
  checkState(state, "", [], -1);

  // Type an X again.  The form history entry should appear.
  state = await msg("key", { key: "x", waitForSuggestions: true });
  checkState(
    state,
    "x",
    [{ str: "x", type: "formHistory" }, "xfoo", "xbar"],
    -1
  );

  // Select the form history entry and delete it.
  state = await msg("key", "VK_DOWN");
  checkState(
    state,
    "x",
    [{ str: "x", type: "formHistory" }, "xfoo", "xbar"],
    0
  );

  // Wait for Satchel.
  observePromise = new Promise(resolve => {
    Services.obs.addObserver(function onRemove(subj, topic, data) {
      if (data == "formhistory-remove") {
        Services.obs.removeObserver(onRemove, "satchel-storage-changed");
        executeSoon(resolve);
      }
    }, "satchel-storage-changed");
  });
  state = await msg("key", "VK_DELETE");
  checkState(state, "x", ["xfoo", "xbar"], -1);

  await observePromise;

  // Reset the input.
  state = await msg("reset");
  checkState(state, "", [], -1);

  // Type an X again.  The form history entry should still be gone.
  state = await msg("key", { key: "x", waitForSuggestions: true });
  checkState(state, "x", ["xfoo", "xbar"], -1);

  await msg("reset");
});

add_task(async function formHistory_limit() {
  info("Check long strings are not added to form history");
  await focusContentSearchBar();
  const gLongString = new Array(
    SearchSuggestionController.SEARCH_HISTORY_MAX_VALUE_LENGTH + 1
  )
    .fill("x")
    .join("");
  // Type and confirm a very long string.
  let state = await msg("text", {
    value: gLongString,
    waitForSuggestions: true,
  });
  checkState(
    state,
    gLongString,
    [`${gLongString}foo`, `${gLongString}bar`],
    -1
  );

  await FormHistoryTestUtils.clear("searchbar-history");
  let entry = await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    return Cu.waiveXrays(content).gController.addInputValueToFormHistory();
  });
  // There's nothing we can wait for, since addition should not be happening.
  /* eslint-disable mozilla/no-arbitrary-setTimeout */
  await new Promise(resolve => setTimeout(resolve, 500));
  Assert.equal(
    await FormHistoryTestUtils.count("searchbar-history", {
      source: entry.source,
    }),
    0
  );

  await msg("reset");
});

add_task(async function cycleEngines() {
  await focusContentSearchBar();
  await msg("key", { key: "VK_DOWN", waitForSuggestions: true });

  Services.telemetry.clearEvents();
  Services.fog.testResetFOG();

  let p = SearchTestUtils.promiseSearchNotification(
    "engine-default",
    "browser-search-engine-modified"
  );
  await msg("key", { key: "VK_DOWN", modifiers: { accelKey: true } });
  let newEngine = await p;
  Assert.equal(
    newEngine.name,
    TEST_ENGINE2.name,
    "Should have correctly cycled the engine"
  );

  let snapshot = await Glean.searchEngineDefault.changed.testGetValue();
  delete snapshot[0].timestamp;
  Assert.deepEqual(
    snapshot[0],
    {
      category: "search.engine.default",
      name: "changed",
      extra: {
        new_load_path: TEST_ENGINE2.loadPath,
        previous_engine_id: TEST_ENGINE1.id,
        change_reason: "user_searchbar",
        new_engine_id: TEST_ENGINE2.id,
        new_display_name: TEST_ENGINE2.name,
        new_submission_url: "",
      },
    },
    "Should have received the correct event details"
  );

  p = SearchTestUtils.promiseSearchNotification(
    "engine-default",
    "browser-search-engine-modified"
  );
  await msg("key", { key: "VK_UP", modifiers: { accelKey: true } });
  newEngine = await p;
  Assert.equal(
    newEngine.name,
    TEST_ENGINE1.name,
    "Should have correctly cycled the engine"
  );

  snapshot = await Glean.searchEngineDefault.changed.testGetValue();
  delete snapshot[1].timestamp;
  Assert.deepEqual(
    snapshot[1],
    {
      category: "search.engine.default",
      name: "changed",
      extra: {
        new_load_path: TEST_ENGINE1.loadPath,
        previous_engine_id: TEST_ENGINE2.id,
        change_reason: "user_searchbar",
        new_engine_id: TEST_ENGINE1.id,
        new_display_name: TEST_ENGINE1.name,
        new_submission_url: "",
      },
    },
    "Should have received the correct event details"
  );

  await msg("reset");
});

add_task(async function search() {
  await focusContentSearchBar();

  let modifiers = {};
  ["altKey", "ctrlKey", "metaKey", "shiftKey"].forEach(
    k => (modifiers[k] = true)
  );

  // Test typing a query and pressing enter.
  let p = waitForSearch();
  await msg("key", { key: "x", waitForSuggestions: true });
  await msg("key", { key: "VK_RETURN", modifiers });
  let mesg = await p;
  let eventData = {
    engineName: TEST_ENGINE1.name,
    searchString: "x",
    healthReportKey: "test",
    searchPurpose: "test",
    originalEvent: modifiers,
  };
  SimpleTest.isDeeply(eventData, mesg, "Search event data");

  await promiseTab();
  await focusContentSearchBar();

  // Test typing a query, then selecting a suggestion and pressing enter.
  p = waitForSearch();
  await msg("key", { key: "x", waitForSuggestions: true });
  await msg("key", "VK_DOWN");
  await msg("key", "VK_DOWN");
  await msg("key", { key: "VK_RETURN", modifiers });
  mesg = await p;
  eventData.searchString = "xfoo";
  eventData.engineName = TEST_ENGINE1.name;
  eventData.selection = {
    index: 1,
    kind: "key",
  };
  SimpleTest.isDeeply(eventData, mesg, "Search event data");

  await promiseTab();
  await focusContentSearchBar();

  // Test typing a query, then selecting a one-off button and pressing enter.
  p = waitForSearch();
  await msg("key", { key: "x", waitForSuggestions: true });
  await msg("key", "VK_UP");
  await msg("key", "VK_UP");
  await msg("key", { key: "VK_RETURN", modifiers });
  mesg = await p;
  delete eventData.selection;
  eventData.searchString = "x";
  eventData.engineName = TEST_ENGINE2.name;
  SimpleTest.isDeeply(eventData, mesg, "Search event data");

  await promiseTab();
  await focusContentSearchBar();

  // Test typing a query and clicking the search engine header.
  p = waitForSearch();
  modifiers.button = 0;
  await msg("key", { key: "x", waitForSuggestions: true });
  await msg("mousemove", -1);
  await msg("click", { eltIdx: -1, modifiers });
  mesg = await p;
  eventData.originalEvent = modifiers;
  eventData.engineName = TEST_ENGINE1.name;
  SimpleTest.isDeeply(eventData, mesg, "Search event data");

  await promiseTab();
  await focusContentSearchBar();

  // Test typing a query and then clicking a suggestion.
  await msg("key", { key: "x", waitForSuggestions: true });
  p = waitForSearch();
  await msg("mousemove", 1);
  await msg("click", { eltIdx: 1, modifiers });
  mesg = await p;
  eventData.searchString = "xfoo";
  eventData.selection = {
    index: 1,
    kind: "mouse",
  };
  SimpleTest.isDeeply(eventData, mesg, "Search event data");

  await promiseTab();
  await focusContentSearchBar();

  // Test typing a query and then clicking a one-off button.
  await msg("key", { key: "x", waitForSuggestions: true });
  p = waitForSearch();
  await msg("mousemove", 3);
  await msg("click", { eltIdx: 3, modifiers });
  mesg = await p;
  eventData.searchString = "x";
  eventData.engineName = TEST_ENGINE2.name;
  delete eventData.selection;
  SimpleTest.isDeeply(eventData, mesg, "Search event data");

  await promiseTab();
  await focusContentSearchBar();

  // Test selecting a suggestion, then clicking a one-off without deselecting the
  // suggestion, using the keyboard.
  delete modifiers.button;
  await msg("key", { key: "x", waitForSuggestions: true });
  p = waitForSearch();
  await msg("key", "VK_DOWN");
  await msg("key", "VK_DOWN");
  await msg("key", "VK_TAB");
  await msg("key", { key: "VK_RETURN", modifiers });
  mesg = await p;
  eventData.searchString = "xfoo";
  eventData.selection = {
    index: 1,
    kind: "key",
  };
  SimpleTest.isDeeply(eventData, mesg, "Search event data");

  await promiseTab();
  await focusContentSearchBar();

  // Test searching when using IME composition.
  let state = await msg("startComposition", { data: "" });
  checkState(state, "", [], -1);
  state = await msg("changeComposition", {
    data: "x",
    waitForSuggestions: true,
  });
  checkState(
    state,
    "x",
    [
      { str: "x", type: "formHistory" },
      { str: "xfoo", type: "formHistory" },
      "xbar",
    ],
    -1
  );
  await msg("commitComposition");
  delete modifiers.button;
  p = waitForSearch();
  await msg("key", { key: "VK_RETURN", modifiers });
  mesg = await p;
  eventData.searchString = "x";
  eventData.originalEvent = modifiers;
  eventData.engineName = TEST_ENGINE1.name;
  delete eventData.selection;
  SimpleTest.isDeeply(eventData, mesg, "Search event data");

  await promiseTab();
  await focusContentSearchBar();

  state = await msg("startComposition", { data: "" });
  checkState(state, "", [], -1);
  state = await msg("changeComposition", {
    data: "x",
    waitForSuggestions: true,
  });
  checkState(
    state,
    "x",
    [
      { str: "x", type: "formHistory" },
      { str: "xfoo", type: "formHistory" },
      "xbar",
    ],
    -1
  );

  // Mouse over the first suggestion.
  state = await msg("mousemove", 0);
  checkState(
    state,
    "x",
    [
      { str: "x", type: "formHistory" },
      { str: "xfoo", type: "formHistory" },
      "xbar",
    ],
    0
  );

  // Mouse over the second suggestion.
  state = await msg("mousemove", 1);
  checkState(
    state,
    "x",
    [
      { str: "x", type: "formHistory" },
      { str: "xfoo", type: "formHistory" },
      "xbar",
    ],
    1
  );

  modifiers.button = 0;
  p = waitForSearch();
  await msg("click", { eltIdx: 1, modifiers });
  mesg = await p;
  eventData.searchString = "xfoo";
  eventData.originalEvent = modifiers;
  eventData.selection = {
    index: 1,
    kind: "mouse",
  };
  SimpleTest.isDeeply(eventData, mesg, "Search event data");

  await promiseTab();
  await focusContentSearchBar();

  // Remove form history entries.
  // Wait for Satchel.
  let observePromise = new Promise(resolve => {
    let historyCount = 2;
    Services.obs.addObserver(function onRemove(subj, topic, data) {
      if (data == "formhistory-remove") {
        if (--historyCount) {
          return;
        }
        Services.obs.removeObserver(onRemove, "satchel-storage-changed");
        executeSoon(resolve);
      }
    }, "satchel-storage-changed");
  });

  await msg("key", { key: "x", waitForSuggestions: true });
  await msg("key", "VK_DOWN");
  await msg("key", "VK_DOWN");
  await msg("key", "VK_DELETE");
  await msg("key", "VK_DOWN");
  await msg("key", "VK_DELETE");
  await observePromise;

  await msg("reset");
  state = await msg("key", { key: "x", waitForSuggestions: true });
  checkState(state, "x", ["xfoo", "xbar"], -1);

  await promiseTab();
  await focusContentSearchBar();
  await msg("reset");
});

add_task(async function settings() {
  await focusContentSearchBar();
  await msg("key", { key: "VK_DOWN", waitForSuggestions: true });
  await msg("key", "VK_UP");
  let p = waitForSearchSettings();
  await msg("key", "VK_RETURN");
  await p;

  await msg("reset");
});

function checkState(
  actualState,
  expectedInputVal,
  expectedSuggestions,
  expectedSelectedIdx,
  expectedSelectedButtonIdx
) {
  expectedSuggestions = expectedSuggestions.map(sugg => {
    return typeof sugg == "object"
      ? sugg
      : {
          str: sugg,
          type: "remote",
        };
  });

  if (expectedSelectedIdx == -1 && expectedSelectedButtonIdx != undefined) {
    expectedSelectedIdx =
      expectedSuggestions.length + expectedSelectedButtonIdx;
  }

  let expectedState = {
    selectedIndex: expectedSelectedIdx,
    numSuggestions: expectedSuggestions.length,
    suggestionAtIndex: expectedSuggestions.map(s => s.str),
    isFormHistorySuggestionAtIndex: expectedSuggestions.map(
      s => s.type == "formHistory"
    ),

    tableHidden: !expectedSuggestions.length,

    inputValue: expectedInputVal,
    ariaExpanded: !expectedSuggestions.length ? "false" : "true",
  };
  if (expectedSelectedButtonIdx != undefined) {
    expectedState.selectedButtonIndex = expectedSelectedButtonIdx;
  } else if (expectedSelectedIdx < expectedSuggestions.length) {
    expectedState.selectedButtonIndex = -1;
  } else {
    expectedState.selectedButtonIndex =
      expectedSelectedIdx - expectedSuggestions.length;
  }

  SimpleTest.isDeeply(actualState, expectedState, "State");
}

var gMsgMan;
var tabs = [];
async function promiseTab() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  tabs.push(tab);

  let loadedPromise = BrowserTestUtils.firstBrowserLoaded(window);
  openTrustedLinkIn("about:test-about-content-search-ui", "current");
  await loadedPromise;
}
