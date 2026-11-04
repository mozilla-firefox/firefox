/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if showing raw headers works.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(POST_DATA_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  // Execute requests.
  await performRequests(monitor, tab, 2);

  let onDomUpdated = waitForDOM(document, "#headers-panel .accordion-item", 2);
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  await onDomUpdated;

  onDomUpdated = waitForDOM(document, "#responseHeaders textarea.raw-headers");
  EventUtils.sendMouseEvent({ type: "click" }, getRawHeadersToggle("RESPONSE"));
  await onDomUpdated;

  onDomUpdated = waitForDOM(document, "#requestHeaders textarea.raw-headers");
  EventUtils.sendMouseEvent({ type: "click" }, getRawHeadersToggle("REQUEST"));
  await onDomUpdated;

  testRawHeaderToggleStyle(true);
  testShowRawHeaders(getSortedRequests(store.getState())[0]);

  EventUtils.sendMouseEvent({ type: "click" }, getRawHeadersToggle("RESPONSE"));
  EventUtils.sendMouseEvent({ type: "click" }, getRawHeadersToggle("REQUEST"));

  testRawHeaderToggleStyle(false);
  testHideRawHeaders(document);

  return teardown(monitor);

  /**
   * Tests that checked is applied correctly
   *
   * @param checked
   *        flag indicating whether toggle is checked or not
   */
  function testRawHeaderToggleStyle(checked) {
    const rawHeadersRequestToggle = getRawHeadersToggle("REQUEST");
    const rawHeadersResponseToggle = getRawHeadersToggle("RESPONSE");

    if (checked) {
      is(
        rawHeadersRequestToggle.checked,
        true,
        "The 'Raw Request Headers' toggle should be 'checked'"
      );
      is(
        rawHeadersResponseToggle.checked,
        true,
        "The 'Raw Response Headers' toggle should be 'checked'"
      );
    } else {
      is(
        rawHeadersRequestToggle.checked,
        false,
        "The 'Raw Request Headers' toggle should NOT be 'checked'"
      );
      is(
        rawHeadersResponseToggle.checked,
        false,
        "The 'Raw Response Headers' toggle should NOT be 'checked'"
      );
    }
  }

  /*
   * Tests that raw headers were displayed correctly
   */
  function testShowRawHeaders(data) {
    // Request headers are rendered first, so it is element with index 1
    const requestHeaders = document.querySelectorAll("textarea.raw-headers")[1]
      .value;
    for (const header of data.requestHeaders.headers) {
      ok(
        requestHeaders.includes(header.name + ": " + header.value),
        "textarea contains request headers"
      );
    }
    // Response headers are rendered first, so it is element with index 0
    const responseHeaders = document.querySelectorAll("textarea.raw-headers")[0]
      .value;
    for (const header of data.responseHeaders.headers) {
      ok(
        responseHeaders.includes(header.name + ": " + header.value),
        "textarea contains response headers"
      );
    }
  }

  /*
   * Tests that raw headers textareas are hidden
   */
  function testHideRawHeaders() {
    ok(
      !document.querySelector(".raw-headers-container"),
      "raw request headers textarea is empty"
    );
  }

  /**
   * Returns the 'Raw Headers' button
   */
  function getRawHeadersToggle(rawHeaderType) {
    if (rawHeaderType === "RESPONSE") {
      // Response header is first displayed
      return document.querySelectorAll(".devtools-checkbox-toggle")[0];
    }
    return document.querySelectorAll(".devtools-checkbox-toggle")[1];
  }
});
