/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that navigation request to a data uri is correctly logged in the
 * network monitor.
 */
add_task(async function test_navigation_to_data_uri() {
  const URL = "data:text/html,Hello from data-url!";
  const { monitor } = await initNetMonitor(URL, {
    requestCount: 1,
    waitForLoad: false,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const wait = waitForNetworkEvents(monitor, 1);
  reloadBrowser({ waitForLoad: false });
  await wait;

  const firstItem = document.querySelectorAll(".request-list-item")[0];

  is(
    firstItem.querySelector(".requests-list-url").innerText,
    URL,
    "The url in the displayed request is correct"
  );
  is(
    firstItem.querySelector(".requests-list-scheme").innerText,
    "data",
    "The scheme in the displayed request is correct"
  );
  is(
    firstItem.querySelector(".requests-list-file").innerText,
    URL,
    "The file in the displayed request is correct"
  );
  ok(hasValidSize(firstItem), "The request shows a valid size");

  await teardown(monitor);
});

/**
 * Tests that requests to data URIs made from a content page are logged in the
 * network monitor.
 */
add_task(async function test_content_request_to_data_uri() {
  const IMAGE_URL =
    "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7";
  const URL = `https://example.com/document-builder.sjs?html=
  <h1>Test page for content data uri request</h1>`;

  const { monitor, tab } = await initNetMonitor(URL, {
    requestCount: 1,
    waitForLoad: false,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  let onNetworkEvents = waitForNetworkEvents(monitor, 1);
  reloadBrowser({ waitForLoad: false });
  await onNetworkEvents;

  info("Load an image in content with a data URI");
  onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await SpecialPowers.spawn(tab.linkedBrowser, [IMAGE_URL], imageURL => {
    const img = content.document.createElement("img");
    img.src = imageURL;
    content.document.body.appendChild(img);
  });
  await onNetworkEvents;

  const firstItem = document.querySelectorAll(".request-list-item")[1];

  is(
    firstItem.querySelector(".requests-list-url").innerText,
    IMAGE_URL,
    "The url in the displayed request is correct"
  );
  is(
    firstItem.querySelector(".requests-list-scheme").innerText,
    "data",
    "The scheme in the displayed request is correct"
  );
  is(
    firstItem.querySelector(".requests-list-file").innerText,
    IMAGE_URL,
    "The file in the displayed request is correct"
  );
  ok(hasValidSize(firstItem), "The request shows a valid size");

  await teardown(monitor);
});
