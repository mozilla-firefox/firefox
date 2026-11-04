/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test the script caches in the network monitor.
async function do_test_script_cache(enableCache) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.script_loader.experimental.navigation_cache", enableCache],
      [
        "dom.script_loader.experimental.navigation_cache.check_memory_pressure",
        false,
      ],
    ],
  });
  registerCleanupFunction(() => SpecialPowers.popPrefEnv());

  const { monitor } = await initNetMonitor(SCRIPT_CACHE_URL, {
    enableCache: true,
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const { getDisplayedRequests, getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  const waitForEvents = waitForNetworkEvents(monitor, 4);

  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    SCRIPT_CACHE_URL
  );
  await waitForEvents;

  const requests = document.querySelectorAll(".request-list-item");

  is(requests.length, 4, "There should be 4 requests");

  const requestData = {
    uri: HTTPS_EXAMPLE_URL + "sjs_test-script.sjs",
    details: {
      status: 200,
      statusText: "OK (cached)",
      displayedStatus: "cached",
      type: "js",
      fullMimeType: "text/javascript",
    },
  };

  is(requests[0].querySelector(".requests-list-type").textContent, "html");

  // All script elements should create corresponding requests.
  for (let i = 1; i < requests.length; ++i) {
    const request = requests[i];

    const requestsListStatus = request.querySelector(".status-code");
    EventUtils.sendMouseEvent({ type: "mouseover" }, requestsListStatus);
    await waitUntil(() => requestsListStatus.title);

    await verifyRequestItemTarget(
      document,
      getDisplayedRequests(store.getState()),
      getSortedRequests(store.getState())[i],
      "GET",
      requestData.uri,
      requestData.details
    );

    clickOnRequestRow(request);

    const onDomUpdated = waitForDOM(document, "#responseHeaders");
    clickOnSidebarTab(document, "headers");
    await onDomUpdated;

    const responseScope = document.querySelectorAll(
      "#headers-panel .accordion tr[id^='/responseHeaders']"
    );

    const responseHeaders = [
      {
        name: "cache-control",
        value: "max-age=10000",
        index: 1,
      },
      {
        name: "connection",
        value: "close",
        index: 2,
      },
      {
        name: "content-length",
        value: "29",
        index: 3,
      },
      {
        name: "content-type",
        value: "text/javascript",
        index: 4,
      },
      {
        name: "server",
        value: "httpd.js",
        index: 6,
      },
    ];
    responseHeaders.forEach(header => {
      is(
        responseScope[header.index - 1].querySelector(".treeLabel").innerHTML,
        header.name,
        `${header.name} label`
      );
      is(
        responseScope[header.index - 1].querySelector(".objectBox").innerHTML,
        `${header.value}`,
        `${header.name} value`
      );
    });
  }
  await teardown(monitor);
}

add_task(async function test_script_cache_disabled() {
  await do_test_script_cache(false);
});

add_task(async function test_script_cache_enabled() {
  await do_test_script_cache(true);
});

/**
 * Importing the same module script multiple times within the same document
 * should not generate multiple fetch, regardless of whether the script cache
 * is used or not, and there should be only one request shown in the network
 * monitor.
 */
async function do_test_module_cache(enableCache) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.script_loader.experimental.navigation_cache", enableCache],
      [
        "dom.script_loader.experimental.navigation_cache.check_memory_pressure",
        false,
      ],
    ],
  });
  registerCleanupFunction(() => SpecialPowers.popPrefEnv());

  const { monitor } = await initNetMonitor(MODULE_SCRIPT_CACHE_URL, {
    enableCache: true,
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const { getDisplayedRequests, getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  const waitForEvents = waitForNetworkEvents(monitor, 2);

  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    MODULE_SCRIPT_CACHE_URL
  );
  await waitForEvents;

  const requests = document.querySelectorAll(".request-list-item");

  is(requests.length, 2, "There should be 2 requests");

  const requestData = {
    uri: HTTPS_EXAMPLE_URL + "sjs_test-module-script.sjs",
    details: {
      status: 200,
      statusText: "OK (cached)",
      displayedStatus: "cached",
      type: "js",
      fullMimeType: "text/javascript",
    },
  };

  is(requests[0].querySelector(".requests-list-type").textContent, "html");

  const request = requests[1];

  const requestsListStatus = request.querySelector(".status-code");
  EventUtils.sendMouseEvent({ type: "mouseover" }, requestsListStatus);
  await waitUntil(() => requestsListStatus.title);

  await verifyRequestItemTarget(
    document,
    getDisplayedRequests(store.getState()),
    getSortedRequests(store.getState())[1],
    "GET",
    requestData.uri,
    requestData.details
  );

  clickOnRequestRow(request);

  const onDomUpdated = waitForDOM(document, "#responseHeaders");
  clickOnSidebarTab(document, "headers");
  await onDomUpdated;

  const responseScope = document.querySelectorAll(
    "#headers-panel .accordion tr[id^='/responseHeaders']"
  );

  const responseHeaders = [
    {
      name: "cache-control",
      value: "max-age=10000",
      index: 1,
    },
    {
      name: "connection",
      value: "close",
      index: 2,
    },
    {
      name: "content-length",
      value: "53",
      index: 3,
    },
    {
      name: "content-type",
      value: "text/javascript",
      index: 4,
    },
    {
      name: "server",
      value: "httpd.js",
      index: 6,
    },
  ];
  responseHeaders.forEach(header => {
    is(
      responseScope[header.index - 1].querySelector(".treeLabel").innerHTML,
      header.name,
      `${header.name} label`
    );
    is(
      responseScope[header.index - 1].querySelector(".objectBox").innerHTML,
      `${header.value}`,
      `${header.name} value`
    );
  });
  await teardown(monitor);
}

add_task(async function test_module_cache_disabled() {
  await do_test_module_cache(false);
});

add_task(async function test_module_cache_enabled() {
  await do_test_module_cache(true);
});
