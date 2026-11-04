/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if Request-Headers and Response-Headers are correctly filtered in Headers tab.
 */
add_task(async function () {
  const { monitor } = await initNetMonitor(SIMPLE_SJS, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  const onHeadersOverview = waitUntil(() =>
    document.querySelector(".headers-overview")
  );
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  await onHeadersOverview;

  await waitForRequestData(store, ["requestHeaders", "responseHeaders"]);

  document.querySelectorAll(".devtools-filterinput")[1].focus();
  EventUtils.synthesizeKey("con", {});
  await waitUntil(() => document.querySelector(".treeRow.hidden"));

  info("Check if Headers are filtered correctly");

  const expectedResponseHeaders = [
    "cache-control",
    "connection",
    "content-length",
    "content-type",
  ];
  const expectedRequestHeaders = ["Cache-Control", "Connection"];

  const responseLabelCells = document.querySelectorAll(
    "#responseHeaders .treeLabelCell"
  );
  const requestLabelCells = document.querySelectorAll(
    "#requestHeaders .treeLabelCell"
  );
  const filteredResponseHeaders = [];
  const filteredRequestHeaders = [];

  for (let i = 0; i < responseLabelCells.length; i++) {
    if (responseLabelCells[i].offsetHeight > 0) {
      filteredResponseHeaders.push(responseLabelCells[i].innerText);
    }
  }

  for (let i = 0; i < requestLabelCells.length; i++) {
    if (requestLabelCells[i].offsetHeight > 0) {
      filteredRequestHeaders.push(requestLabelCells[i].innerText);
    }
  }

  is(
    filteredResponseHeaders.toString(),
    expectedResponseHeaders.toString(),
    "Response Headers are filtered"
  );
  is(
    filteredRequestHeaders.toString(),
    expectedRequestHeaders.toString(),
    "Request Headers are filtered"
  );

  await teardown(monitor);
});
