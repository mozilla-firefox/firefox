/* Any copyright is dedicated to the Public Domain.
  http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests the url preview expanded state is persisted across requests selections.
 */

add_task(async function () {
  const { monitor, tab } = await initNetMonitor(PARAMS_URL, {
    requestCount: 1,
  });

  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  // Execute requests.
  await performRequests(monitor, tab, 12);

  let onDomUpdated = waitForDOM(document, "#headers-panel .url-preview", 1);
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  await onDomUpdated;

  // Expand preview
  await toggleUrlPreview(true, monitor);

  // Select the second request
  onDomUpdated = waitForDOM(document, "#headers-panel .url-preview", 1);
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[1]);
  await onDomUpdated;

  // Test that the url is still expanded
  const noOfVisibleRowsAfterExpand = document.querySelectorAll(
    "#headers-panel .url-preview tr.treeRow"
  ).length;
  Assert.greater(
    noOfVisibleRowsAfterExpand,
    1,
    "The url preview should still be expanded."
  );

  // Collapse preview
  await toggleUrlPreview(false, monitor);

  // Select the third request
  onDomUpdated = waitForDOM(document, "#headers-panel .url-preview", 1);
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[2]);
  await onDomUpdated;

  // Test that the url is still collapsed
  const noOfVisibleRowsAfterCollapse = document.querySelectorAll(
    "#headers-panel .url-preview tr.treeRow"
  ).length;
  Assert.equal(
    noOfVisibleRowsAfterCollapse,
    1,
    "The url preview should still be collapsed."
  );

  return teardown(monitor);
});

/**
 *  Checks if the query parameter arrays are formatted as we expected.
 */

add_task(async function () {
  const { monitor } = await initNetMonitor(PARAMS_URL, {
    requestCount: 1,
  });

  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const netWorkEvent = waitForNetworkEvents(monitor, 3);
  await performRequestsInContent([
    // URL with same parameter name with different values
    { url: "sjs_content-type-test-server.sjs?a=3&a=45&a=60" },

    // URL with mix of different parameter names
    { url: "sjs_content-type-test-server.sjs?x=5&a=3&a=4&a=3&b=3" },

    // URL contains a parameter with `query` as the name. This makes sure
    // there is no conflict with the query property on the Url Object in the
    // UrlPreview
    { url: "sjs_content-type-test-server.sjs?x=5&a=3&a=4&a=3&query=3" },

    // URL contains a paramter with `__proto__` as the name. This makes sure
    // there is no conflict with the prototype chain of the JS object.
    { url: "sjs_content-type-test-server.sjs?__proto__=5" },
  ]);
  await netWorkEvent;

  let urlPreview = waitForDOM(document, "#headers-panel .url-preview", 1);
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  let urlPreviewValue = (await urlPreview)[0].textContent;

  ok(
    urlPreviewValue.endsWith("?a=3&a=45&a=60"),
    "The parameters in the url preview match."
  );

  urlPreview = waitForDOM(document, "#headers-panel .url-preview", 1);
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[1]);

  urlPreviewValue = (await urlPreview)[0].textContent;
  ok(
    urlPreviewValue.endsWith("?x=5&a=3&a=4&a=3&b=3"),
    "The parameters in the url preview match."
  );

  urlPreview = waitForDOM(document, "#headers-panel .url-preview", 1);
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[2]);

  urlPreviewValue = (await urlPreview)[0].textContent;
  ok(
    urlPreviewValue.endsWith("?x=5&a=3&a=4&a=3&query=3"),
    "The parameters in the url preview match."
  );

  // Expand preview
  await toggleUrlPreview(true, monitor);

  // Check if the expanded preview contains the "query" parameter
  is(
    document.querySelector(
      "#headers-panel .url-preview tr#\\/GET\\/query\\/query .treeLabelCell"
    ).textContent,
    "query",
    "Contains the query parameter"
  );

  // Collapse preview
  await toggleUrlPreview(false, monitor);

  urlPreview = waitForDOM(document, "#headers-panel .url-preview", 1);
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[3]);

  urlPreviewValue = (await urlPreview)[0].textContent;
  ok(
    urlPreviewValue.endsWith("?__proto__=5"),
    "The parameters in the url preview match."
  );

  // Expand preview
  await toggleUrlPreview(true, monitor);

  // Check if the expanded preview contains the "__proto__" parameter
  is(
    document.querySelector(
      "#headers-panel .url-preview tr#\\/GET\\/query\\/__proto__ .treeLabelCell"
    ).textContent,
    "__proto__",
    "Contains the __proto__ parameter"
  );

  return teardown(monitor);
});
