/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if Open in new tab works by ContextMenu.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(OPEN_REQUEST_IN_TAB_URL, {
    requestCount: 1,
  });
  info("Starting test...");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  let newTab;

  store.dispatch(Actions.batchEnable(false));

  // Post data may be fetched by the Header panel,
  // so set the Timings panel as the new default.
  store.getState().ui.detailsPanelSelectedTab = "timings";

  // Open GET request in new tab
  await performRequest(monitor, tab, "GET");
  newTab = await openLastRequestInTab();
  await checkTabResponse(newTab, "GET");
  checkNewTabIndex(tab, newTab);
  gBrowser.removeCurrentTab();

  // Open POST request in new tab
  await performRequest(
    monitor,
    tab,
    "POST",
    "application/x-www-form-urlencoded",
    "foo=bar&baz=42"
  );
  newTab = await openLastRequestInTab();
  await checkTabResponse(
    newTab,
    "POST",
    "application/x-www-form-urlencoded",
    "foo=bar&amp;baz=42"
  );
  checkNewTabIndex(tab, newTab);
  gBrowser.removeCurrentTab();

  // Open POST application/json request in new tab
  await performRequest(
    monitor,
    tab,
    "POST",
    "application/json",
    '{"foo":"bar"}'
  );
  newTab = await openLastRequestInTab();
  await checkTabResponse(newTab, "POST", "application/json", '{"foo":"bar"}');
  checkNewTabIndex(tab, newTab);
  gBrowser.removeCurrentTab();

  await teardown(monitor);

  // OpenLastRequestInTab by ContextMenu
  async function openLastRequestInTab() {
    const requestItems = document.querySelectorAll(".request-list-item");
    const lastRequest = requestItems[requestItems.length - 1];
    clickOnRequestRow(lastRequest);
    EventUtils.sendMouseEvent({ type: "contextmenu" }, lastRequest);

    const onTabOpen = once(gBrowser.tabContainer, "TabOpen", false);
    await selectContextMenuItem(monitor, "request-list-context-newtab");
    await onTabOpen;
    info("A new tab has been opened");

    const awaitedTab = gBrowser.selectedTab;
    await BrowserTestUtils.browserLoaded(awaitedTab.linkedBrowser);
    info("The tab load completed");

    return awaitedTab;
  }
});

/**
 * Tests if Open in new tab works by DoubleClick RequestItem.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(OPEN_REQUEST_IN_TAB_URL, {
    requestCount: 1,
  });
  info("Starting test...");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  let newTab;

  store.dispatch(Actions.batchEnable(false));

  // Post data may be fetched by the Header panel,
  // so set the Timings panel as the new default.
  store.getState().ui.detailsPanelSelectedTab = "timings";

  // Open GET request in new tab
  await performRequest(monitor, tab, "GET");
  newTab = await openLastRequestInTab();
  await checkTabResponse(newTab, "GET");
  checkNewTabIndex(tab, newTab);
  gBrowser.removeCurrentTab();

  // Open POST request in new tab
  await performRequest(
    monitor,
    tab,
    "POST",
    "application/x-www-form-urlencoded",
    "foo=bar&baz=42"
  );
  newTab = await openLastRequestInTab();
  await checkTabResponse(
    newTab,
    "POST",
    "application/x-www-form-urlencoded",
    "foo=bar&amp;baz=42"
  );
  checkNewTabIndex(tab, newTab);
  gBrowser.removeCurrentTab();

  // Open POST application/json request in new tab
  await performRequest(
    monitor,
    tab,
    "POST",
    "application/json",
    '{"foo":"bar"}'
  );
  newTab = await openLastRequestInTab();
  await checkTabResponse(newTab, "POST", "application/json", '{"foo":"bar"}');
  checkNewTabIndex(tab, newTab);
  gBrowser.removeCurrentTab();

  await teardown(monitor);

  // OpenLastRequestInTab by DoubleClick
  async function openLastRequestInTab() {
    const requestItems = document.querySelectorAll(".request-list-item");
    const lastRequest = requestItems[requestItems.length - 1];

    const onTabOpen = once(gBrowser.tabContainer, "TabOpen", false);
    EventUtils.sendMouseEvent({ type: "dblclick" }, lastRequest);
    await onTabOpen;
    info("A new tab has been opened");

    const awaitedTab = gBrowser.selectedTab;
    await BrowserTestUtils.browserLoaded(awaitedTab.linkedBrowser);
    info("The tab load completed");

    return awaitedTab;
  }
});

/**
 * Tests if Open in new tab works by middle click RequestItem.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(OPEN_REQUEST_IN_TAB_URL, {
    requestCount: 1,
  });
  const MIDDLE_MOUSE_BUTTON = 1;
  info("Starting test...");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  let newTab;

  store.dispatch(Actions.batchEnable(false));

  // Post data may be fetched by the Header panel,
  // so set the Timings panel as the new default.
  store.getState().ui.detailsPanelSelectedTab = "timings";

  // Open GET request in new tab
  await performRequest(monitor, tab, "GET");
  newTab = await openLastRequestInTab();
  await checkTabResponse(newTab, "GET");
  checkNewTabIndex(tab, newTab);
  gBrowser.removeCurrentTab();

  // Open POST request in new tab
  await performRequest(
    monitor,
    tab,
    "POST",
    "application/x-www-form-urlencoded",
    "foo=bar&baz=42"
  );
  newTab = await openLastRequestInTab();
  await checkTabResponse(
    newTab,
    "POST",
    "application/x-www-form-urlencoded",
    "foo=bar&amp;baz=42"
  );
  checkNewTabIndex(tab, newTab);
  gBrowser.removeCurrentTab();

  // Open POST application/json request in new tab
  await performRequest(
    monitor,
    tab,
    "POST",
    "application/json",
    '{"foo":"bar"}'
  );
  newTab = await openLastRequestInTab();
  await checkTabResponse(newTab, "POST", "application/json", '{"foo":"bar"}');
  checkNewTabIndex(tab, newTab);
  gBrowser.removeCurrentTab();

  await teardown(monitor);

  // OpenLastRequestInTab by middle click
  async function openLastRequestInTab() {
    const requestItems = document.querySelectorAll(".request-list-item");
    const lastRequest = requestItems[requestItems.length - 1];

    const onTabOpen = once(gBrowser.tabContainer, "TabOpen", false);
    clickOnRequestRow(lastRequest, { button: MIDDLE_MOUSE_BUTTON });
    await onTabOpen;
    info("A new tab has been opened");

    const awaitedTab = gBrowser.selectedTab;
    await BrowserTestUtils.browserLoaded(awaitedTab.linkedBrowser);
    info("The tab load completed");

    return awaitedTab;
  }
});

async function performRequest(monitor, tab, method, contentType, payload) {
  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [[method, contentType, payload]],
    async function ([method_, contentType_, payload_]) {
      content.wrappedJSObject.performRequest(method_, contentType_, payload_);
    }
  );
  await onNetworkEvents;
  info("Performed request to test server");
}

async function checkTabResponse(checkedTab, method, contentType, payload) {
  await SpecialPowers.spawn(
    checkedTab.linkedBrowser,
    [[method, contentType, payload]],
    async function ([method_, contentType_, payload_]) {
      const { body } = content.wrappedJSObject.document;
      const expected = [method_, contentType_, payload_].join("\n");
      info("Response from the server:" + body.innerHTML.replace(/\n/g, "\\n"));
      ok(
        body.innerHTML.includes(expected),
        "Tab method and data match original request"
      );
    }
  );
}

async function checkNewTabIndex(currentTab, newTab) {
  Assert.strictEqual(
    newTab.index,
    currentTab.index + 1,
    "The new tab opens next to the current one"
  );
}
