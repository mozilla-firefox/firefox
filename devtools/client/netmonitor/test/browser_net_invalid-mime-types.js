/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that a page with an invalid mimetype does not crash devtools.
 */
add_task(async function testInvalidMimeTypeDoesNotCrash() {
  const URL = "data:text/__proto__,hello";
  const { monitor } = await initNetMonitor(URL, {
    requestCount: 1,
    waitForLoad: false,
  });

  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  reloadBrowser({ waitForLoad: false });
  await onNetworkEvents;

  const firstItem = document.querySelector(".request-list-item");

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
    firstItem.querySelector(".requests-list-type").innerText,
    "__proto__",
    "The type in the displayed request is correct"
  );

  await teardown(monitor);
});

/**
 * Tests that a page with an uppercase mimetype shows the correct type.
 */
add_task(async function testUpperCaseMimeType() {
  const URL = "data:TEXT/JAVASCRIPT,hello";
  const { monitor } = await initNetMonitor(URL, {
    requestCount: 1,
    waitForLoad: false,
  });

  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  reloadBrowser({ waitForLoad: false });
  await onNetworkEvents;

  const firstItem = document.querySelector(".request-list-item");

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
    firstItem.querySelector(".requests-list-type").innerText,
    "js",
    "The type in the displayed request is correct"
  );

  await teardown(monitor);
});
