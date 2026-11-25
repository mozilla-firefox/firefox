/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests the helpers that the openWindows cleaner uses for its keepPinnedTabs
// option: clearing open windows for real would close the test's own window.

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

async function addLoadedTab(win, url, options = {}) {
  let tab = BrowserTestUtils.addTab(win.gBrowser, url, options);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, url);
  return tab;
}

add_task(async function keepsPinnedTabsOfNonPrivateWindows() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.userContext.enabled", true]],
  });
  const FIRST_URL =
    "https://example.com/browser/browser/base/content/test/browser-sanitize/dummy_page.html";
  const SECOND_URL = "https://example.com/";
  const CONTAINER_URL = "https://example.org/";

  // A pinned tab that navigated from the page itself, so that its current
  // entry has a referrer and a content triggering principal, with back
  // history, page state and sessionStorage.
  let tabWithHistory = await addLoadedTab(window, FIRST_URL);
  gBrowser.selectedTab = tabWithHistory;
  await SpecialPowers.spawn(tabWithHistory.linkedBrowser, [SECOND_URL], url => {
    let link = content.document.createElement("a");
    link.href = url;
    link.id = "link";
    link.textContent = "link";
    content.document.body.append(link);
  });
  let loaded = BrowserTestUtils.browserLoaded(
    tabWithHistory.linkedBrowser,
    false,
    SECOND_URL
  );
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#link",
    {},
    tabWithHistory.linkedBrowser
  );
  await loaded;
  await SpecialPowers.spawn(tabWithHistory.linkedBrowser, [], async () => {
    content.history.replaceState({ page: "state" }, "");
    content.sessionStorage.setItem("key", "value");
  });
  tabWithHistory.toggleMuteAudio();
  gBrowser.pinTab(tabWithHistory);
  await TabStateFlusher.flush(tabWithHistory.linkedBrowser);
  let originalState = JSON.parse(SessionStore.getTabState(tabWithHistory));
  let originalEntry = originalState.entries[originalState.index - 1];
  Assert.greater(originalState.entries.length, 1, "The tab has history");
  Assert.ok(originalEntry.referrerInfo, "The tab has a referrer");
  Assert.ok(originalEntry.structuredCloneState, "The tab has page state");
  Assert.ok(originalState.storage, "The tab has sessionStorage");

  let otherWin = await BrowserTestUtils.openNewBrowserWindow();
  let containerTab = await addLoadedTab(otherWin, CONTAINER_URL, {
    userContextId: 1,
  });
  otherWin.gBrowser.pinTab(containerTab);
  await addLoadedTab(otherWin, "https://example.net/");

  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  let privateTab = await addLoadedTab(privateWin, "https://example.net/");
  privateWin.gBrowser.pinTab(privateTab);

  // Like the openWindows cleaner does before collecting.
  for (let win of [window, otherWin, privateWin]) {
    await TabStateFlusher.flushWindow(win);
  }
  let { openWindows } = Sanitizer.items;
  let pinnedTabs = openWindows._collectPinnedTabs([
    window,
    otherWin,
    privateWin,
  ]);
  Assert.deepEqual(
    pinnedTabs.map(state => [state.entries[0].url, state.userContextId]),
    [
      [SECOND_URL, 0],
      [CONTAINER_URL, 1],
    ],
    "Collects the pinned tabs of non-private windows in window order"
  );

  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  openWindows._restorePinnedTabs(newWin, pinnedTabs);
  let restored = newWin.gBrowser.tabs.filter(tab => tab.pinned);
  Assert.deepEqual(
    restored.map(tab => [tab.linkedBrowser.currentURI.spec, tab.userContextId]),
    [
      [SECOND_URL, 0],
      [CONTAINER_URL, 1],
    ],
    "Both pinned tabs are restored with their address and container"
  );
  for (let tab of restored) {
    Assert.ok(!tab.linkedPanel, "The tab is not loaded");
  }

  let [restoredTab] = restored;
  Assert.equal(restoredTab.label, tabWithHistory.label, "Keeps the title");
  Assert.ok(restoredTab.muted, "Stays muted");
  let state = JSON.parse(SessionStore.getTabState(restoredTab));
  Assert.equal(state.entries.length, 1, "Has no back or forward history");
  let [entry] = state.entries;
  Assert.equal(
    entry.triggeringPrincipal_base64,
    originalEntry.triggeringPrincipal_base64,
    "Keeps the triggering principal"
  );
  Assert.equal(
    entry.referrerInfo,
    originalEntry.referrerInfo,
    "Keeps the referrer"
  );
  Assert.ok(!entry.structuredCloneState, "Drops the page state");
  Assert.ok(!state.storage, "Drops sessionStorage");

  for (let win of [newWin, privateWin, otherWin]) {
    await BrowserTestUtils.closeWindow(win);
  }
  BrowserTestUtils.removeTab(tabWithHistory);
  await SpecialPowers.popPrefEnv();
});

/**
 * Restoring runs while the sanitizer waits for the new window, so a tab that
 * fails to restore must not throw and keep the other tabs from being added.
 */
add_task(async function skipsTabsThatFailToRestore() {
  const URL = "https://example.com/";
  let tab = await addLoadedTab(window, URL);
  await TabStateFlusher.flush(tab.linkedBrowser);
  let { openWindows } = Sanitizer.items;
  let state = openWindows._scrubTabState(
    JSON.parse(SessionStore.getTabState(tab))
  );
  state.pinned = true;
  BrowserTestUtils.removeTab(tab);

  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  openWindows._restorePinnedTabs(newWin, [{}, state]);
  let [firstTab] = newWin.gBrowser.tabs;
  Assert.ok(firstTab.pinned, "The valid tab is restored, pinned, first");
  Assert.equal(
    firstTab.linkedBrowser.currentURI.spec,
    URL,
    "The valid tab keeps its address"
  );
  Assert.equal(
    newWin.gBrowser.tabs.filter(t => t.pinned).length,
    1,
    "The invalid state adds no tab"
  );
  await BrowserTestUtils.closeWindow(newWin);
});

/**
 * The pinned tabs are only added back once everything else is cleared, so
 * that selecting one can't load anything while clearing is still running.
 */
add_task(async function restoresAfterClearing() {
  const URL = "https://example.com/";
  let tab = await addLoadedTab(window, URL);
  await TabStateFlusher.flush(tab.linkedBrowser);
  let { items } = Sanitizer;
  let state = items.openWindows._scrubTabState(
    JSON.parse(SessionStore.getTabState(tab))
  );
  state.pinned = true;
  BrowserTestUtils.removeTab(tab);

  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  // Clearing open windows for real would close the test's own window.
  let originalClear = items.openWindows.clear;
  items.openWindows.clear = async () => ({
    newWindow: newWin,
    pinnedTabs: [state],
  });
  let { promise: slowCleared, resolve: finishSlowCleaner } =
    Promise.withResolvers();
  let slowCleanerStarted = false;
  items.testSlowCleaner = {
    clear() {
      slowCleanerStarted = true;
      return slowCleared;
    },
  };
  let pinnedCount = () => newWin.gBrowser.tabs.filter(t => t.pinned).length;
  try {
    let sanitized = Sanitizer.sanitize(["openWindows", "testSlowCleaner"], {
      keepPinnedTabs: true,
    });
    await TestUtils.waitForCondition(
      () => slowCleanerStarted,
      "Waiting for the other cleaners to start."
    );
    Assert.equal(pinnedCount(), 0, "No pinned tab while clearing runs");
    finishSlowCleaner();
    await sanitized;
    Assert.equal(pinnedCount(), 1, "The pinned tab is added once cleared");
  } finally {
    items.openWindows.clear = originalClear;
    delete items.testSlowCleaner;
  }
  await BrowserTestUtils.closeWindow(newWin);
});

add_task(async function scrubsSessionData() {
  let state = Sanitizer.items.openWindows._scrubTabState({
    entries: [
      { url: "https://example.com/1", title: "1" },
      {
        url: "https://example.com/2",
        title: "2",
        triggeringPrincipal_base64: "principal",
        referrerInfo: "referrer",
        children: [{ url: "https://example.org/frame" }],
        presState: [{}],
        scroll: "0,10",
        structuredCloneState: "state",
        structuredCloneVersion: 8,
      },
      { url: "https://example.com/3", title: "3" },
    ],
    index: 2,
    requestedIndex: 3,
    pinned: true,
    muted: true,
    muteReason: "user",
    userContextId: 2,
    attributes: { customizemode: "true" },
    image: "icon",
    extData: { key: "value" },
    formdata: { id: { field: "typed" } },
    scroll: { scroll: "0,10" },
    searchMode: { engineName: "engine" },
    storage: { "https://example.com": { key: "value" } },
    userTypedClear: 1,
    userTypedValue: "typed",
  });
  Assert.deepEqual(
    state,
    {
      entries: [
        {
          url: "https://example.com/2",
          title: "2",
          triggeringPrincipal_base64: "principal",
          referrerInfo: "referrer",
        },
      ],
      index: 1,
      pinned: true,
      muted: true,
      muteReason: "user",
      userContextId: 2,
      attributes: { customizemode: "true" },
      image: "icon",
    },
    "Keeps the current entry and the tab's own state only"
  );
});
