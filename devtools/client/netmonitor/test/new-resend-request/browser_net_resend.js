/* Any copyright is dedicated to the Public Domain.
 *  http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if resending a request works.
 */

add_task(async function () {
  if (
    Services.prefs.getBoolPref(
      "devtools.netmonitor.features.newEditAndResend",
      true
    )
  ) {
    await testResendRequest();
  } else {
    await testOldEditAndResendPanel();
  }
});

// This tests resending a request without editing using
// the resend context menu item. This particularly covering
// the new resend functionality.
async function testResendRequest() {
  const { tab, monitor } = await initNetMonitor(POST_DATA_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;

  // Action should be processed synchronously in tests.
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  await performRequests(monitor, tab, 2);

  is(
    document.querySelectorAll(".request-list-item").length,
    2,
    "There are currently two requests"
  );

  const firstResend = await resendRequestAndWaitForNewRequest(
    monitor,
    document.querySelectorAll(".request-list-item")[0]
  );

  Assert.notStrictEqual(
    firstResend.originalResource.resourceId,
    firstResend.newResource.resourceId,
    "The resent request is different resource from the first request"
  );

  is(
    firstResend.originalResource.url,
    firstResend.newResource.url,
    "The resent request has the same url and query parameters and the first request"
  );

  // The priority header only appears when the urgency and incremental values
  // are not both default values (u=3 and i=false). In this case the original
  // request has no priority header and the resent request does, hence we subtract one.
  is(
    firstResend.originalResource.requestHeaders.headers.length,
    firstResend.newResource.requestHeaders.headers.length - 1,
    "The no of headers are the same"
  );

  // Because a resent request has a different purpose and principal it will
  // also have a different CoS flag (meaning a different priority header).
  // So we can't compare the original and resent request's priority and skip it.
  firstResend.originalResource.requestHeaders.headers.forEach(
    ({ name, value }) => {
      if (name === "Priority") {
        return;
      }
      const foundHeader = firstResend.newResource.requestHeaders.headers.find(
        header => header.name == name
      );
      is(
        value,
        foundHeader.value,
        `The '${name}' header for the request and the resent request match`
      );
    }
  );

  info("Check that the custom headers and form data are resent correctly");
  const secondResend = await resendRequestAndWaitForNewRequest(
    monitor,
    document.querySelectorAll(".request-list-item")[1]
  );

  Assert.notStrictEqual(
    secondResend.originalResource.resourceId,
    secondResend.newResource.resourceId,
    "The resent request is different resource from the second request"
  );

  const customHeader =
    secondResend.originalResource.requestHeaders.headers.find(
      header => header.name == "custom-header-xxx"
    );

  const customHeaderInResentRequest =
    secondResend.newResource.requestHeaders.headers.find(
      header => header.name == "custom-header-xxx"
    );

  is(
    customHeader.value,
    customHeaderInResentRequest.value,
    "The custom header in the resent request is the same as the second request"
  );

  is(
    customHeaderInResentRequest.value,
    "custom-value-xxx",
    "The custom header in the resent request is correct"
  );

  is(
    secondResend.originalResource.requestPostData.postData.text,
    secondResend.newResource.requestPostData.postData.text,
    "The form data in the resent is the same as the second request"
  );
}

async function resendRequestAndWaitForNewRequest(monitor, originalRequestItem) {
  const { document, store, windowRequire, connector } = monitor.panelWin;
  const { getSelectedRequest, getDisplayedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  info("Select the request to resend");
  const expectedNoOfRequestsAfterResend =
    getDisplayedRequests(store.getState()).length + 1;

  const waitForHeaders = waitUntil(() =>
    document.querySelector(".headers-overview")
  );
  clickOnRequestRow(originalRequestItem);
  await waitForHeaders;

  const originalResourceId = getSelectedRequest(store.getState()).id;

  const waitForNewRequest = waitUntil(
    () =>
      getDisplayedRequests(store.getState()).length ==
        expectedNoOfRequestsAfterResend &&
      getSelectedRequest(store.getState()).id !== originalResourceId
  );

  info("Open the context menu and select the resend for the request");
  EventUtils.sendMouseEvent({ type: "contextmenu" }, originalRequestItem);
  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await selectContextMenuItem(monitor, "request-list-context-resend-only");
  await waitForNewRequest;
  await onNetworkEvents;

  const newResourceId = getSelectedRequest(store.getState()).id;

  // Make sure we fetch the request headers and post data for the
  // new request so we can assert them.
  await connector.requestData(newResourceId, "requestHeaders");
  await connector.requestData(newResourceId, "requestPostData");

  return {
    originalResource: getRequestById(store.getState(), originalResourceId),
    newResource: getRequestById(store.getState(), newResourceId),
  };
}

// This is a basic test for the old edit and resend panel
// This should be removed soon in Bug 1745416 when we remove
// the old panel functionality.
async function testOldEditAndResendPanel() {
  const ADD_QUERY = "t1=t2";
  const ADD_HEADER = "Test-header: true";
  const ADD_UA_HEADER = "User-Agent: Custom-Agent";
  const ADD_POSTDATA = "&t3=t4";

  const { tab, monitor } = await initNetMonitor(POST_DATA_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getSelectedRequest, getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  // Execute requests.
  await performRequests(monitor, tab, 2);

  const origItemId = getSortedRequests(store.getState())[0].id;

  store.dispatch(Actions.selectRequest(origItemId));
  await waitForRequestData(
    store,
    ["requestHeaders", "requestPostData"],
    origItemId
  );

  let origItem = getSortedRequests(store.getState())[0];

  // add a new custom request cloned from selected request

  store.dispatch(Actions.cloneSelectedRequest());
  await testCustomForm(origItem);

  let customItem = getSelectedRequest(store.getState());
  testCustomItem(customItem, origItem);

  // edit the custom request
  await editCustomForm();

  // FIXME: reread the customItem, it's been replaced by a new object (immutable!)
  customItem = getSelectedRequest(store.getState());
  testCustomItemChanged(customItem, origItem);

  // send the new request
  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  store.dispatch(Actions.sendCustomRequest());
  await onNetworkEvents;

  let sentItem;
  // Testing sent request will require updated requestHeaders and requestPostData,
  // we must wait for both properties get updated before starting test.
  await waitUntil(() => {
    sentItem = getSelectedRequest(store.getState());
    origItem = getSortedRequests(store.getState())[0];
    return (
      sentItem &&
      sentItem.requestHeaders &&
      sentItem.requestPostData &&
      origItem &&
      origItem.requestHeaders &&
      origItem.requestPostData
    );
  });

  await testSentRequest(sentItem, origItem);

  // Ensure the UI shows the new request, selected, and that the detail panel was closed.
  is(
    getSortedRequests(store.getState()).length,
    3,
    "There are 3 requests shown"
  );
  is(
    document
      .querySelector(".request-list-item.selected")
      .getAttribute("data-id"),
    sentItem.id,
    "The sent request is selected"
  );
  is(
    document.querySelector(".network-details-bar"),
    null,
    "The detail panel is hidden"
  );

  await teardown(monitor);

  function testCustomItem(item, orig) {
    is(
      item.method,
      orig.method,
      "item is showing the same method as original request"
    );
    is(item.url, orig.url, "item is showing the same URL as original request");
  }

  function testCustomItemChanged(item, orig) {
    const { url } = item;
    const expectedUrl = orig.url + "&" + ADD_QUERY;

    is(url, expectedUrl, "menu item is updated to reflect url entered in form");
  }

  /*
   * Test that the New Request form was populated correctly
   */
  async function testCustomForm(data) {
    await waitUntil(() => document.querySelector(".custom-request-panel"));
    is(
      document.getElementById("custom-method-value").value,
      data.method,
      "new request form showing correct method"
    );

    is(
      document.getElementById("custom-url-value").value,
      data.url,
      "new request form showing correct url"
    );

    const query = document.getElementById("custom-query-value");
    is(
      query.value,
      "foo=bar\nbaz=42\ntype=urlencoded",
      "new request form showing correct query string"
    );

    const headers = document
      .getElementById("custom-headers-value")
      .value.split("\n");
    for (const { name, value } of data.requestHeaders.headers) {
      ok(
        headers.includes(name + ": " + value),
        "form contains header from request"
      );
    }

    const postData = document.getElementById("custom-postdata-value");
    is(
      postData.value,
      data.requestPostData.postData.text,
      "new request form showing correct post data"
    );
  }

  /*
   * Add some params and headers to the request form
   */
  async function editCustomForm() {
    monitor.panelWin.focus();

    const query = document.getElementById("custom-query-value");
    const queryFocus = once(query, "focus", false);
    // Bug 1195825: Due to some unexplained dark-matter with promise,
    // focus only works if delayed by one tick.
    query.setSelectionRange(query.value.length, query.value.length);
    executeSoon(() => query.focus());
    await queryFocus;

    // add params to url query string field
    typeInNetmonitor(["VK_RETURN"], monitor);
    typeInNetmonitor(ADD_QUERY, monitor);

    const headers = document.getElementById("custom-headers-value");
    const headersFocus = once(headers, "focus", false);
    headers.setSelectionRange(headers.value.length, headers.value.length);
    headers.focus();
    await headersFocus;

    // add a header
    typeInNetmonitor(["VK_RETURN"], monitor);
    typeInNetmonitor(ADD_HEADER, monitor);

    // add a User-Agent header, to check if default headers can be modified
    // (there will be two of them, first gets overwritten by the second)
    typeInNetmonitor(["VK_RETURN"], monitor);
    typeInNetmonitor(ADD_UA_HEADER, monitor);

    const postData = document.getElementById("custom-postdata-value");
    const postFocus = once(postData, "focus", false);
    postData.setSelectionRange(postData.value.length, postData.value.length);
    postData.focus();
    await postFocus;

    // add to POST data once textarea has updated
    await waitUntil(() => postData.textContent !== "");
    typeInNetmonitor(ADD_POSTDATA, monitor);
  }

  /*
   * Make sure newly created event matches expected request
   */
  async function testSentRequest(data, origData) {
    is(data.method, origData.method, "correct method in sent request");
    is(data.url, origData.url + "&" + ADD_QUERY, "correct url in sent request");

    const { headers } = data.requestHeaders;
    const hasHeader = headers.some(h => `${h.name}: ${h.value}` == ADD_HEADER);
    ok(hasHeader, "new header added to sent request");

    const hasUAHeader = headers.some(
      h => `${h.name}: ${h.value}` == ADD_UA_HEADER
    );
    ok(hasUAHeader, "User-Agent header added to sent request");

    is(
      data.requestPostData.postData.text,
      origData.requestPostData.postData.text + ADD_POSTDATA,
      "post data added to sent request"
    );
  }
}
