/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";
/**
 * Test that the URL Preview can be copied
 */
add_task(async function () {
  const { monitor } = await initNetMonitor(SIMPLE_URL, {
    requestCount: 1,
  });

  info("Starting the url preview copy test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  store.dispatch(Actions.toggleNetworkDetails());

  await waitForDOM(document, "#headers-panel .url-preview", 1);

  const urlPreview = document.querySelector("#headers-panel .url-preview");
  const urlRow = urlPreview.querySelector(".objectRow");

  /* Test for copy value on the url */
  EventUtils.sendMouseEvent({ type: "contextmenu" }, urlRow);
  await waitForClipboardPromise(async function setup() {
    await selectContextMenuItem(
      monitor,
      "properties-view-context-menu-copyvalue"
    );
  }, "http://example.com/browser/devtools/client/netmonitor/test/html_simple-test-page.html");

  ok(true, "The copy value action put expected url string into clipboard");

  /* Test for copy all */
  EventUtils.sendMouseEvent({ type: "contextmenu" }, urlRow);
  const expected = JSON.stringify(
    {
      GET: {
        scheme: "http",
        host: "example.com",
        filename:
          "/browser/devtools/client/netmonitor/test/html_simple-test-page.html",
        remote: {
          Address: "127.0.0.1:8888",
        },
      },
    },
    null,
    "\t"
  );
  await waitForClipboardPromise(async function setup() {
    await selectContextMenuItem(
      monitor,
      "properties-view-context-menu-copyall"
    );
  }, expected);

  ok(true, "The copy all action put expected json data into clipboard");

  await teardown(monitor);
});

/**
 * Test that the Headers summary can be copied
 */

add_task(async function () {
  const { monitor } = await initNetMonitor(SIMPLE_URL, {
    requestCount: 1,
  });

  info("Starting the headers summary copy test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  store.dispatch(Actions.toggleNetworkDetails());

  await waitForDOM(document, "#headers-panel .summary", 1);

  const headersSummary = document.querySelector("#headers-panel .summary");
  const httpSummaryValue = headersSummary.querySelectorAll(
    ".tabpanel-summary-value"
  )[1];

  /* Test for copy value */
  EventUtils.sendMouseEvent({ type: "contextmenu" }, httpSummaryValue);
  await waitForClipboardPromise(async function setup() {
    await selectContextMenuItem(
      monitor,
      "headers-panel-context-menu-copyvalue"
    );
  }, "HTTP/1.1");

  ok(true, "The copy value action put expected text into clipboard");

  /* Test for copy all */
  EventUtils.sendMouseEvent({ type: "contextmenu" }, httpSummaryValue);
  const expected = JSON.stringify(
    {
      Status: "200OK",
      Version: "HTTP/1.1",
      Transferred: "650 B (465 B size)",
      "Request Priority": "Highest",
      "DNS Resolution": "System",
    },
    null,
    "\t"
  );
  await waitForClipboardPromise(async function setup() {
    await selectContextMenuItem(monitor, "headers-panel-context-menu-copyall");
  }, expected);

  ok(true, "The copy all action put expected json into clipboard");

  await teardown(monitor);
});

/**
 * Test if response JSON in PropertiesView can be copied
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(
    JSON_BASIC_URL + "?name=nogrip",
    { requestCount: 1 }
  );
  info("Starting the json in properties view copy test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  await performRequests(monitor, tab, 1);

  const onResponsePanelReady = waitForDOM(
    document,
    "#response-panel .treeTable"
  );
  store.dispatch(Actions.toggleNetworkDetails());
  clickOnSidebarTab(document, "response");
  await onResponsePanelReady;

  const responsePanel = document.querySelector("#response-panel");

  const objectRow = responsePanel.querySelectorAll(".objectRow")[0];

  // Open the node to get the string
  const waitOpenNode = waitForDOM(document, ".stringRow");
  const toggleButton = objectRow.querySelector("td span.treeIcon");
  toggleButton.click();
  await waitOpenNode;
  const stringRow = responsePanel.querySelectorAll(".stringRow")[0];

  /* Test for copy value on an object */
  EventUtils.sendMouseEvent({ type: "contextmenu" }, objectRow);
  const expected = JSON.stringify({ obj: { type: "string" } }, null, "\t");
  await waitForClipboardPromise(async function setup() {
    await selectContextMenuItem(
      monitor,
      "properties-view-context-menu-copyvalue"
    );
  }, expected);

  ok(true, "The copy value action put expected json into clipboard");

  /* Test for copy all */
  EventUtils.sendMouseEvent({ type: "contextmenu" }, objectRow);
  await waitForClipboardPromise(async function setup() {
    await selectContextMenuItem(
      monitor,
      "properties-view-context-menu-copyall"
    );
  }, expected);

  ok(true, "The copy all action put expected json into clipboard");

  /* Test for copy value of a single row */
  EventUtils.sendMouseEvent({ type: "contextmenu" }, stringRow);
  await waitForClipboardPromise(async function setup() {
    await selectContextMenuItem(
      monitor,
      "properties-view-context-menu-copyvalue"
    );
  }, "string");

  ok(true, "The copy value action put expected text into clipboard");

  await teardown(monitor);
});

/**
 * Test if response/request Cookies in PropertiesView can be copied
 */

add_task(async function () {
  const { monitor } = await initNetMonitor(SIMPLE_UNSORTED_COOKIES_SJS, {
    requestCount: 1,
  });
  info(
    "Starting the request/response  cookies in properties view copy test... "
  );

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  const onDomUpdated = waitForDOM(document, ".headers-overview");
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  await onDomUpdated;

  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  clickOnSidebarTab(document, "cookies");

  const cookiesPanel = document.querySelector("#cookies-panel");

  const objectRows = cookiesPanel.querySelectorAll(".objectRow");
  const stringRows = cookiesPanel.querySelectorAll(".stringRow");

  const expectedResponseCookies = [
    `{
	"__proto__": {
		"httpOnly": true,
		"value": "2"
	}
}`,
    `{
	"bob": {
		"httpOnly": true,
		"value": "true"
	}
}`,
    `{
	"foo": {
		"httpOnly": true,
		"value": "bar"
	}
}`,
    `{
	"tom": {
		"httpOnly": true,
		"value": "cool"
	}
}`,
  ];
  for (let i = 0; i < objectRows.length; i++) {
    const cur = objectRows[i];
    EventUtils.sendMouseEvent({ type: "contextmenu" }, cur);
    await waitForClipboardPromise(async function setup() {
      await selectContextMenuItem(
        monitor,
        "properties-view-context-menu-copyvalue"
      );
    }, expectedResponseCookies[i]);
  }

  const expectedRequestCookies = ["2", "true", "bar", "cool"];
  for (let i = 0; i < expectedRequestCookies.length; i++) {
    const cur = stringRows[objectRows.length + i];
    EventUtils.sendMouseEvent({ type: "contextmenu" }, cur);
    await waitForClipboardPromise(async function setup() {
      await selectContextMenuItem(
        monitor,
        "properties-view-context-menu-copyvalue"
      );
    }, expectedRequestCookies[i]);
  }

  await teardown(monitor);
});
