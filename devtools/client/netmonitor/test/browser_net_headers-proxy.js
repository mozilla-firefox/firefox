/* Any copyright is dedicated to the Public Domain.
  http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that the proxy information is displayed in the netmonitor
add_task(async function () {
  const { monitor } = await initNetMonitor(HTTPS_CUSTOM_GET_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document } = monitor.panelWin;

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  // Wait until the tab panel summary is displayed
  const waitForTab = waitUntil(() =>
    document.querySelector(".tabpanel-summary-label")
  );
  clickOnRequestRow(document.querySelector(".request-list-item"));
  await waitForTab;

  // Expand preview
  await toggleUrlPreview(true, monitor);

  const proxyAddressEl = [...document.querySelectorAll(".treeRow")].find(
    el => el.querySelector(".treeLabelCell")?.textContent === "Proxy Address"
  );

  is(
    proxyAddressEl.querySelector(".treeValueCell").innerText,
    "127.0.0.1:4443",
    "The remote proxy address summary value is correct."
  );

  is(
    document.querySelector(".headers-proxy-status .headers-summary-label")
      .textContent,
    "Proxy Status",
    "The proxy status header is displayed"
  );

  is(
    document.querySelector(".headers-proxy-status .tabpanel-summary-value")
      .textContent,
    "200Connected",
    "The proxy status value showed correctly"
  );

  is(
    document.querySelector(".headers-proxy-version .tabpanel-summary-label")
      .textContent,
    "Proxy Version",
    "The proxy http version header is displayed"
  );

  is(
    document.querySelector(".headers-proxy-version .tabpanel-summary-value")
      .textContent,
    "HTTP/1.1",
    "The proxy http version value showed correctly"
  );

  await teardown(monitor);
});

const noProxyServerUrl = createTestHTTPServer();
noProxyServerUrl.registerPathHandler("/index.html", (request, response) => {
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/html", true);
  response.write("<html> SIMPLE DOCUMENT </html>");
});

const NO_PROXY_SERVER_URL = `http://localhost:${noProxyServerUrl.identity.primaryPort}`;

// Test that the proxy information is not displayed in the netmonitor
add_task(async function () {
  const { monitor } = await initNetMonitor(NO_PROXY_SERVER_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document } = monitor.panelWin;

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  // Wait until the tab panel summary is displayed
  const waitForTab = waitUntil(() =>
    document.querySelector(".tabpanel-summary-label")
  );
  clickOnRequestRow(document.querySelector(".request-list-item"));
  await waitForTab;

  // Expand preview
  await toggleUrlPreview(true, monitor);

  const addressEl = [...document.querySelectorAll(".treeRow")].find(
    el => el.querySelector(".treeLabelCell")?.textContent === "Address"
  );

  ok(addressEl, "The address is not the proxy address");

  ok(
    !document.querySelector(".headers-proxy-status"),
    "The proxy status header is not displayed"
  );

  ok(
    !document.querySelector(".headers-proxy-version"),
    "The proxy http version header is not displayed"
  );

  await teardown(monitor);
});

const serverBehindFakeProxy = createTestHTTPServer();
const fakeProxy = createTestHTTPServer();

fakeProxy.identity.add(
  "http",
  "localhost",
  serverBehindFakeProxy.identity.primaryPort
);
fakeProxy.registerPrefixHandler("/", (request, response) => {
  if (request.hasHeader("Proxy-Authorization")) {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.setHeader("Content-Type", "text/html", true);
    response.write("ok, got proxy auth");
  } else {
    response.setStatusLine(
      request.httpVersion,
      407,
      "Proxy authentication required"
    );
    response.setHeader("Content-Type", "text/plain", false);
    response.setHeader("Proxy-Authenticate", 'Basic realm="foobar"', false);
    response.write("auth required");
  }
});

const SERVER_URL = `http://localhost:${serverBehindFakeProxy.identity.primaryPort}`;

// Test that `Proxy-Authorization` request header is not shown for in the headers panel
add_task(async function () {
  await pushPref("network.proxy.type", 1);
  await pushPref("network.proxy.http", "localhost");
  await pushPref("network.proxy.http_port", fakeProxy.identity.primaryPort);
  await pushPref("network.proxy.allow_hijacking_localhost", true);

  // Wait for initial primary password dialog after opening the tab.
  const onDialog = TestUtils.topicObserved("common-dialog-loaded");

  const tab = await addTab(SERVER_URL, { waitForLoad: false });

  const toolbox = await gDevTools.showToolboxForTab(tab, {
    toolId: "netmonitor",
  });
  info("Network monitor pane shown successfully.");

  const monitor = toolbox.getCurrentPanel();
  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  const [subject] = await onDialog;
  const dialog = subject.Dialog;

  ok(true, `Authentication dialog displayed`);

  info("Fill in login and password, and validate dialog");
  dialog.ui.loginTextbox.value = "user";
  dialog.ui.password1Textbox.value = "pass";

  const onDialogClosed = BrowserTestUtils.waitForEvent(
    window,
    "DOMModalDialogClosed"
  );
  dialog.ui.button0.click();
  await onDialogClosed;
  ok(true, "Dialog is closed");

  const requestEl = await waitFor(() =>
    document.querySelector(".request-list-item")
  );
  clickOnRequestRow(requestEl);

  await waitUntil(() => document.querySelector(".headers-overview"));

  const headersPanel = document.querySelector("#headers-panel");
  const headerIsFound = [
    ...headersPanel.querySelectorAll("tr .treeLabelCell .treeLabel"),
  ].some(headerEl => headerEl.innerText == "Proxy-Authorization");

  ok(
    !headerIsFound,
    "The `Proxy-Authorization` should not be displayed in the Headers panel"
  );

  await teardown(monitor);
});
