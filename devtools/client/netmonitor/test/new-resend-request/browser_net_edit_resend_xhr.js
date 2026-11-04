/* Any copyright is dedicated to the Public Domain.
 *  http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if editing and resending a XHR request works and the
 * cloned request retains the same cause type.
 */

add_task(async function () {
  if (
    Services.prefs.getBoolPref(
      "devtools.netmonitor.features.newEditAndResend",
      true
    )
  ) {
    ok(
      true,
      "Skip this test when pref is true, because this panel won't be default when that is the case."
    );
    return;
  }

  const { tab, monitor } = await initNetMonitor(POST_RAW_URL, {
    requestCount: 1,
  });

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  // Executes 1 XHR request
  await performRequests(monitor, tab, 1);

  // Selects 1st XHR request
  const xhrRequest = document.querySelectorAll(".request-list-item")[0];
  clickOnRequestRow(xhrRequest);

  // Stores original request for comparison of values later
  const { getSelectedRequest } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );
  const original = getSelectedRequest(store.getState());

  // Context Menu > "Edit & Resend"
  EventUtils.sendMouseEvent({ type: "contextmenu" }, xhrRequest);
  await selectContextMenuItem(monitor, "request-list-context-edit-resend");

  // 1) Wait for "Edit & Resend" panel to appear
  // 2) Click the "Send" button
  // 3) Wait till the new request appears in the list
  await waitUntil(() => document.querySelector(".custom-request-panel"));
  document.querySelector("#custom-request-send-button").click();
  await waitForNetworkEvents(monitor, 1);

  // Selects cloned request
  const clonedRequest = document.querySelectorAll(".request-list-item")[1];
  clickOnRequestRow(clonedRequest);
  const cloned = getSelectedRequest(store.getState());

  // Compares if the requests have the same cause type (XHR)
  Assert.strictEqual(
    original.cause.type,
    cloned.cause.type,
    "Both requests retain the same cause type"
  );

  await teardown(monitor);
});

/**
 * Tests if editing and resending a XHR request works and the
 * new request retains the same cause type.
 */

add_task(async function () {
  if (
    Services.prefs.getBoolPref(
      "devtools.netmonitor.features.newEditAndResend",
      true
    )
  ) {
    const { tab, monitor } = await initNetMonitor(POST_RAW_URL, {
      requestCount: 1,
    });

    const { document, store, windowRequire } = monitor.panelWin;
    const Actions = windowRequire(
      "devtools/client/netmonitor/src/actions/index"
    );
    store.dispatch(Actions.batchEnable(false));

    // Executes 1 XHR request
    await performRequests(monitor, tab, 1);

    // Selects 1st XHR request
    const xhrRequest = document.querySelectorAll(".request-list-item")[0];
    clickOnRequestRow(xhrRequest);

    // Stores original request for comparison of values later
    const { getSelectedRequest } = windowRequire(
      "devtools/client/netmonitor/src/selectors/index"
    );
    const original = getSelectedRequest(store.getState());

    // Context Menu > "Edit & Resend"
    EventUtils.sendMouseEvent({ type: "contextmenu" }, xhrRequest);
    await selectContextMenuItem(monitor, "request-list-context-edit-resend");

    // 1) Wait for "Edit & Resend" panel to appear
    // 2) Wait for the Send button to be  enabled (i.e all the data is loaded)
    // 2) Click the "Send" button
    // 3) Wait till the new request appears in the list
    await waitUntil(
      () =>
        document.querySelector(".http-custom-request-panel") &&
        document.querySelector("#http-custom-request-send-button").disabled ===
          false
    );
    document.querySelector("#http-custom-request-send-button").click();
    await waitForNetworkEvents(monitor, 1);

    // Selects new request
    const newRequest = document.querySelectorAll(".request-list-item")[1];
    clickOnRequestRow(newRequest);
    const request = getSelectedRequest(store.getState());

    Assert.strictEqual(
      original.cause.type,
      request.cause.type,
      "Both requests retain the same cause type"
    );

    await teardown(monitor);
  }
});

/**
 * Tests that resending a XHR request uses the same security deatils as the
 * original request.
 */

add_task(async function () {
  if (
    Services.prefs.getBoolPref(
      "devtools.netmonitor.features.newEditAndResend",
      true
    )
  ) {
    const { tab, monitor } = await initNetMonitor(HTTPS_CORS_URL, {
      requestCount: 1,
    });

    const { document, store, windowRequire, connector } = monitor.panelWin;
    const Actions = windowRequire(
      "devtools/client/netmonitor/src/actions/index"
    );
    store.dispatch(Actions.batchEnable(false));

    info("Performing a CORS request");
    const requestUrl = "https://example.com" + CORS_SJS_PATH;

    const onNetworkEvents = waitForNetworkEvents(monitor, 1);
    await SpecialPowers.spawn(
      tab.linkedBrowser,
      [requestUrl],
      async function (url) {
        content.wrappedJSObject.performRequests(url);
      }
    );

    info("Waiting until the requests appear in netmonitor");
    await onNetworkEvents;

    const { getSelectedRequest } = windowRequire(
      "devtools/client/netmonitor/src/selectors/index"
    );

    info("Select XHR request");
    const xhrRequest = document.querySelectorAll(".request-list-item")[0];
    clickOnRequestRow(xhrRequest);

    info("Fetch the Headers for the original XHR request");
    let originalRequest = getSelectedRequest(store.getState());
    await connector.requestData(originalRequest.id, "requestHeaders");
    await waitForRequestData(store, ["requestHeaders"]);
    originalRequest = getSelectedRequest(store.getState());

    info("Resend the XHR request");
    const waitForResentRequest = waitForNetworkEvents(monitor, 1);
    EventUtils.sendMouseEvent({ type: "contextmenu" }, xhrRequest);
    await selectContextMenuItem(monitor, "request-list-context-resend-only");
    await waitForResentRequest;

    info("Fetch the Headers for the resent XHR request");
    let resentRequest = getSelectedRequest(store.getState());
    await connector.requestData(resentRequest.id, "requestHeaders");
    await waitForRequestData(store, ["requestHeaders"]);
    resentRequest = getSelectedRequest(store.getState());

    const originalRequestSecFetchModeHeader =
      originalRequest.requestHeaders.headers.find(
        header => header.name == "Sec-Fetch-Mode"
      );
    const resentRequestSecFetchModeHeader =
      resentRequest.requestHeaders.headers.find(
        header => header.name == "Sec-Fetch-Mode"
      );

    info("Assert the security mode for the original amd resent request");
    Assert.strictEqual(
      originalRequestSecFetchModeHeader.value,
      resentRequestSecFetchModeHeader.value,
      "Both requests retain the same security mode"
    );

    await teardown(monitor);
  }
});
