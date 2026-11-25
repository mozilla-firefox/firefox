/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Session data written by older versions stores the address bar value override
// as `userTypedValue` and `userTypedClear`.
// TODO(bug 2092393): Remove this test along with the conversion.

function legacyTab(userTypedValue, userTypedClear) {
  return { entries: [], userTypedValue, userTypedClear };
}

function assertUpgraded(tabData, value, loadStarted, where) {
  is(tabData.urlbarOverrideValue, value, `${where}: value converted`);
  is(
    tabData.urlbarOverrideLoadStarted,
    loadStarted ? true : undefined,
    `${where}: started load converted`
  );
  ok(
    !("userTypedValue" in tabData) && !("userTypedClear" in tabData),
    `${where}: legacy keys removed`
  );
}

add_task(function test_upgrade_every_location() {
  let windowState = () => ({
    tabs: [legacyTab("open", 0)],
    _closedTabs: [{ state: legacyTab("closed", 1) }],
    closedGroups: [{ tabs: [{ state: legacyTab("closed group", 2) }] }],
  });
  let state = {
    windows: [windowState()],
    _closedWindows: [windowState()],
    savedGroups: [{ tabs: [{ state: legacyTab("saved group", 0) }] }],
    lastSessionState: { windows: [windowState()] },
    deferredInitialState: { windows: [windowState()] },
  };

  TabState.upgradeLegacyUrlbarOverrideInSession(state);

  for (let [where, win] of [
    ["window", state.windows[0]],
    ["closed window", state._closedWindows[0]],
    ["last session", state.lastSessionState.windows[0]],
    ["deferred session", state.deferredInitialState.windows[0]],
  ]) {
    assertUpgraded(win.tabs[0], "open", false, `${where} tab`);
    assertUpgraded(
      win._closedTabs[0].state,
      "closed",
      true,
      `${where} closed tab`
    );
    assertUpgraded(
      win.closedGroups[0].tabs[0].state,
      "closed group",
      true,
      `${where} closed group tab`
    );
  }
  assertUpgraded(
    state.savedGroups[0].tabs[0].state,
    "saved group",
    false,
    "saved group tab"
  );
});

add_task(function test_new_keys_win() {
  let tabData = {
    entries: [],
    urlbarOverrideValue: "new",
    userTypedValue: "old",
    userTypedClear: 1,
  };
  TabState.upgradeLegacyUrlbarOverride(tabData);
  assertUpgraded(tabData, "new", false, "tab with both keys");
});

add_task(async function test_setTabState() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  await promiseTabState(tab, legacyTab("example.com", 0));

  is(
    tab.linkedBrowser.urlbarOverrideValue,
    "example.com",
    "The value was restored to the browser"
  );
  assertUpgraded(
    JSON.parse(ss.getTabState(tab)),
    "example.com",
    false,
    "collected tab state"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_setBrowserState_loads_started_value() {
  let oldState = ss.getBrowserState();

  await promiseBrowserState({
    windows: [{ tabs: [legacyTab("https://example.com", 2)] }],
  });

  is(
    gBrowser.selectedBrowser.currentURI.spec,
    "https://example.com/",
    "A positive userTypedClear loads the value"
  );

  await promiseBrowserState(JSON.parse(oldState));
});
