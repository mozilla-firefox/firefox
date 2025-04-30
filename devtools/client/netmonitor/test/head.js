/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This file (head.js) is injected into all other test contexts within
 * this directory, allowing one to utilize the functions here in said
 * tests without referencing head.js explicitly.
 */

/* exported Toolbox, restartNetMonitor, teardown, waitForExplicitFinish,
   verifyRequestItemTarget, waitFor, waitForDispatch, testFilterButtons,
   performRequestsInContent, waitForNetworkEvents, selectIndexAndWaitForSourceEditor,
   testColumnsAlignment, hideColumn, showColumn, performRequests, waitForRequestData,
   toggleBlockedUrl, registerFaviconNotifier, clickOnSidebarTab */

"use strict";

// The below file (shared-head.js) handles imports, constants, and
// utility functions, and is loaded into this context.
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/shared/test/shared-head.js",
  this
);

const { LinkHandlerParent } = ChromeUtils.importESModule(
  "resource:///actors/LinkHandlerParent.sys.mjs"
);

const {
  getFormattedIPAndPort,
  getFormattedTime,
} = require("resource://devtools/client/netmonitor/src/utils/format-utils.js");

const {
  getSortedRequests,
  getRequestById,
} = require("resource://devtools/client/netmonitor/src/selectors/index.js");

const {
  getUnicodeUrl,
  getUnicodeHostname,
} = require("resource://devtools/client/shared/unicode-url.js");
const {
  getFormattedProtocol,
  getUrlHost,
  getUrlScheme,
} = require("resource://devtools/client/netmonitor/src/utils/request-utils.js");
const {
  EVENTS,
  TEST_EVENTS,
} = require("resource://devtools/client/netmonitor/src/constants.js");
const {
  L10N,
} = require("resource://devtools/client/netmonitor/src/utils/l10n.js");

/* eslint-disable no-unused-vars, max-len */
const EXAMPLE_URL =
  "http://example.com/browser/devtools/client/netmonitor/test/";
const EXAMPLE_ORG_URL =
  "http://example.org/browser/devtools/client/netmonitor/test/";
const HTTPS_EXAMPLE_URL =
  "https://example.com/browser/devtools/client/netmonitor/test/";
const HTTPS_EXAMPLE_ORG_URL =
  "https://example.org/browser/devtools/client/netmonitor/test/";
/* Since the test server will proxy `ws://example.com` to websocket server on 9988,
so we must sepecify the port explicitly */
const WS_URL = "ws://127.0.0.1:8888/browser/devtools/client/netmonitor/test/";
const WS_HTTP_URL =
  "http://127.0.0.1:8888/browser/devtools/client/netmonitor/test/";

const WS_BASE_URL =
  "http://mochi.test:8888/browser/devtools/client/netmonitor/test/";
const WS_PAGE_URL = WS_BASE_URL + "html_ws-test-page.html";
const WS_PAGE_EARLY_CONNECTION_URL =
  WS_BASE_URL + "html_ws-early-connection-page.html";
const API_CALLS_URL = HTTPS_EXAMPLE_URL + "html_api-calls-test-page.html";
const SIMPLE_URL = EXAMPLE_URL + "html_simple-test-page.html";
const HTTPS_SIMPLE_URL = HTTPS_EXAMPLE_URL + "html_simple-test-page.html";
const NAVIGATE_URL = EXAMPLE_URL + "html_navigate-test-page.html";
const CONTENT_TYPE_WITHOUT_CACHE_URL =
  EXAMPLE_URL + "html_content-type-without-cache-test-page.html";
const CONTENT_TYPE_WITHOUT_CACHE_REQUESTS = 8;
const CYRILLIC_URL = EXAMPLE_URL + "html_cyrillic-test-page.html";
const STATUS_CODES_URL = EXAMPLE_URL + "html_status-codes-test-page.html";
const HTTPS_STATUS_CODES_URL =
  HTTPS_EXAMPLE_URL + "html_status-codes-test-page.html";
const POST_DATA_URL = EXAMPLE_URL + "html_post-data-test-page.html";
const POST_ARRAY_DATA_URL = EXAMPLE_URL + "html_post-array-data-test-page.html";
const POST_JSON_URL = EXAMPLE_URL + "html_post-json-test-page.html";
const POST_RAW_URL = EXAMPLE_URL + "html_post-raw-test-page.html";
const POST_RAW_URL_WITH_HASH = EXAMPLE_URL + "html_header-test-page.html";
const POST_RAW_WITH_HEADERS_URL =
  EXAMPLE_URL + "html_post-raw-with-headers-test-page.html";
const PARAMS_URL = EXAMPLE_URL + "html_params-test-page.html";
const JSONP_URL = EXAMPLE_URL + "html_jsonp-test-page.html";
const JSON_LONG_URL = EXAMPLE_URL + "html_json-long-test-page.html";
const JSON_MALFORMED_URL = EXAMPLE_URL + "html_json-malformed-test-page.html";
const JSON_CUSTOM_MIME_URL =
  EXAMPLE_URL + "html_json-custom-mime-test-page.html";
const JSON_TEXT_MIME_URL = EXAMPLE_URL + "html_json-text-mime-test-page.html";
const JSON_B64_URL = EXAMPLE_URL + "html_json-b64.html";
const JSON_BASIC_URL = EXAMPLE_URL + "html_json-basic.html";
const JSON_EMPTY_URL = EXAMPLE_URL + "html_json-empty.html";
const JSON_XSSI_PROTECTION_URL = EXAMPLE_URL + "html_json-xssi-protection.html";
const FONTS_URL = EXAMPLE_URL + "html_fonts-test-page.html";
const SORTING_URL = EXAMPLE_URL + "html_sorting-test-page.html";
const FILTERING_URL = EXAMPLE_URL + "html_filter-test-page.html";
const HTTPS_FILTERING_URL = HTTPS_EXAMPLE_URL + "html_filter-test-page.html";
const INFINITE_GET_URL = EXAMPLE_URL + "html_infinite-get-page.html";
const CUSTOM_GET_URL = EXAMPLE_URL + "html_custom-get-page.html";
const HTTPS_CUSTOM_GET_URL = HTTPS_EXAMPLE_URL + "html_custom-get-page.html";
const SINGLE_GET_URL = EXAMPLE_URL + "html_single-get-page.html";
const HTTPS_SINGLE_GET_URL = HTTPS_EXAMPLE_URL + "html_single-get-page.html";
const STATISTICS_URL = EXAMPLE_URL + "html_statistics-test-page.html";
const STATISTICS_EDGE_CASE_URL =
  EXAMPLE_URL + "html_statistics-edge-case-page.html";
const CURL_URL = EXAMPLE_URL + "html_copy-as-curl.html";
const HTTPS_CURL_URL = HTTPS_EXAMPLE_URL + "html_copy-as-curl.html";
const HTTPS_CURL_UTILS_URL = HTTPS_EXAMPLE_URL + "html_curl-utils.html";
const SEND_BEACON_URL = EXAMPLE_URL + "html_send-beacon.html";
const CORS_URL = EXAMPLE_URL + "html_cors-test-page.html";
const HTTPS_CORS_URL = HTTPS_EXAMPLE_URL + "html_cors-test-page.html";
const PAUSE_URL = EXAMPLE_URL + "html_pause-test-page.html";
const OPEN_REQUEST_IN_TAB_URL = EXAMPLE_URL + "html_open-request-in-tab.html";
const CSP_URL = EXAMPLE_URL + "html_csp-test-page.html";
const CSP_RESEND_URL = EXAMPLE_URL + "html_csp-resend-test-page.html";
const IMAGE_CACHE_URL = HTTPS_EXAMPLE_URL + "html_image-cache.html";
const STYLESHEET_CACHE_URL = HTTPS_EXAMPLE_URL + "html_stylesheet-cache.html";
const SCRIPT_CACHE_URL = HTTPS_EXAMPLE_URL + "html_script-cache.html";
const MODULE_SCRIPT_CACHE_URL =
  HTTPS_EXAMPLE_URL + "html_module-script-cache.html";
const SLOW_REQUESTS_URL = EXAMPLE_URL + "html_slow-requests-test-page.html";
const HTTPS_SLOW_REQUESTS_URL =
  HTTPS_EXAMPLE_URL + "html_slow-requests-test-page.html";

const SIMPLE_SJS = EXAMPLE_URL + "sjs_simple-test-server.sjs";
const HTTPS_SIMPLE_SJS = HTTPS_EXAMPLE_URL + "sjs_simple-test-server.sjs";
const SIMPLE_UNSORTED_COOKIES_SJS =
  EXAMPLE_URL + "sjs_simple-unsorted-cookies-test-server.sjs";
const CONTENT_TYPE_SJS = EXAMPLE_URL + "sjs_content-type-test-server.sjs";
const WS_CONTENT_TYPE_SJS = WS_HTTP_URL + "sjs_content-type-test-server.sjs";
const WS_WS_CONTENT_TYPE_SJS = WS_URL + "sjs_content-type-test-server.sjs";
const HTTPS_CONTENT_TYPE_SJS =
  HTTPS_EXAMPLE_URL + "sjs_content-type-test-server.sjs";
const SERVER_TIMINGS_TYPE_SJS =
  HTTPS_EXAMPLE_URL + "sjs_timings-test-server.sjs";
const STATUS_CODES_SJS = EXAMPLE_URL + "sjs_status-codes-test-server.sjs";
const SORTING_SJS = EXAMPLE_URL + "sjs_sorting-test-server.sjs";
const HTTPS_REDIRECT_SJS = EXAMPLE_URL + "sjs_https-redirect-test-server.sjs";
const CORS_SJS_PATH =
  "/browser/devtools/client/netmonitor/test/sjs_cors-test-server.sjs";
const HSTS_SJS = EXAMPLE_URL + "sjs_hsts-test-server.sjs";
const METHOD_SJS = EXAMPLE_URL + "sjs_method-test-server.sjs";
const HTTPS_SLOW_SJS = HTTPS_EXAMPLE_URL + "sjs_slow-test-server.sjs";
const DELAY_SJS = HTTPS_EXAMPLE_URL + "sjs_delay-test-server.sjs";
const SET_COOKIE_SAME_SITE_SJS = EXAMPLE_URL + "sjs_set-cookie-same-site.sjs";
const SEARCH_SJS = EXAMPLE_URL + "sjs_search-test-server.sjs";
const HTTPS_SEARCH_SJS = HTTPS_EXAMPLE_URL + "sjs_search-test-server.sjs";

const HSTS_BASE_URL = EXAMPLE_URL;
const HSTS_PAGE_URL = CUSTOM_GET_URL;

const TEST_IMAGE = EXAMPLE_URL + "test-image.png";
const TEST_IMAGE_DATA_URI =
  "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAABGdBTUEAAK/INwWK6QAAABl0RVh0U29mdHdhcmUAQWRvYmUgSW1hZ2VSZWFkeXHJZTwAAAHWSURBVHjaYvz//z8DJQAggJiQOe/fv2fv7Oz8rays/N+VkfG/iYnJfyD/1+rVq7ffu3dPFpsBAAHEAHIBCJ85c8bN2Nj4vwsDw/8zQLwKiO8CcRoQu0DxqlWrdsHUwzBAAIGJmTNnPgYa9j8UqhFElwPxf2MIDeIrKSn9FwSJoRkAEEAM0DD4DzMAyPi/G+QKY4hh5WAXGf8PDQ0FGwJ22d27CjADAAIIrLmjo+MXA9R2kAHvGBA2wwx6B8W7od6CeQcggKCmCEL8bgwxYCbUIGTDVkHDBia+CuotgACCueD3TDQN75D4xmAvCoK9ARMHBzAw0AECiBHkAlC0Mdy7x9ABNA3obAZXIAa6iKEcGlMVQHwWyjYuL2d4v2cPg8vZswx7gHyAAAK7AOif7SAbOqCmn4Ha3AHFsIDtgPq/vLz8P4MSkJ2W9h8ggBjevXvHDo4FQUQg/kdypqCg4H8lUIACnQ/SOBMYI8bAsAJFPcj1AAEEjwVQqLpAbXmH5BJjqI0gi9DTAAgDBBCcAVLkgmQ7yKCZxpCQxqUZhAECCJ4XgMl493ug21ZD+aDAXH0WLM4A9MZPXJkJIIAwTAR5pQMalaCABQUULttBGCCAGCnNzgABBgAMJ5THwGvJLAAAAABJRU5ErkJggg==";

const SETTINGS_MENU_ITEMS = {
  "persist-logs": ".netmonitor-settings-persist-item",
  "import-har": ".netmonitor-settings-import-har-item",
  "save-har": ".netmonitor-settings-import-save-item",
  "copy-har": ".netmonitor-settings-import-copy-item",
};

/* eslint-enable no-unused-vars, max-len */

// All tests are asynchronous.
waitForExplicitFinish();

const gEnableLogging = Services.prefs.getBoolPref("devtools.debugger.log");
// To enable logging for try runs, just set the pref to true.
Services.prefs.setBoolPref("devtools.debugger.log", false);

// Uncomment this pref to dump all devtools emitted events to the console.
// Services.prefs.setBoolPref("devtools.dump.emit", true);

// Always reset some prefs to their original values after the test finishes.
const gDefaultFilters = Services.prefs.getCharPref(
  "devtools.netmonitor.filters"
);
const gDefaultRequestFilter = Services.prefs.getCharPref(
  "devtools.netmonitor.requestfilter"
);

// Reveal many columns for test
Services.prefs.setCharPref(
  "devtools.netmonitor.visibleColumns",
  '["initiator","contentSize","cookies","domain","duration",' +
    '"endTime","file","url","latency","method","protocol",' +
    '"remoteip","responseTime","scheme","setCookies",' +
    '"startTime","status","transferred","type","waterfall"]'
);

Services.prefs.setCharPref(
  "devtools.netmonitor.columnsData",
  '[{"name":"override","minWidth":20,"width":2},' +
    '{"name":"status","minWidth":30,"width":5},' +
    '{"name":"method","minWidth":30,"width":5},' +
    '{"name":"domain","minWidth":30,"width":10},' +
    '{"name":"file","minWidth":30,"width":25},' +
    '{"name":"url","minWidth":30,"width":25},' +
    '{"name":"initiator","minWidth":30,"width":20},' +
    '{"name":"type","minWidth":30,"width":5},' +
    '{"name":"transferred","minWidth":30,"width":10},' +
    '{"name":"contentSize","minWidth":30,"width":5},' +
    '{"name":"waterfall","minWidth":150,"width":15}]'
);

registerCleanupFunction(() => {
  info("finish() was called, cleaning up...");

  Services.prefs.setBoolPref("devtools.debugger.log", gEnableLogging);
  Services.prefs.setCharPref("devtools.netmonitor.filters", gDefaultFilters);
  Services.prefs.setCharPref(
    "devtools.netmonitor.requestfilter",
    gDefaultRequestFilter
  );
  Services.prefs.clearUserPref("devtools.cache.disabled");
  Services.prefs.clearUserPref("devtools.netmonitor.columnsData");
  Services.prefs.clearUserPref("devtools.netmonitor.visibleColumns");
  Services.prefs.clearUserPref("devtools.netmonitor.ui.default-raw-response");
  Services.cookies.removeAll();
});

async function disableCacheAndReload(toolbox, waitForLoad) {
  // Disable the cache for any toolbox that it is opened from this point on.
  Services.prefs.setBoolPref("devtools.cache.disabled", true);

  await toolbox.commands.targetConfigurationCommand.updateConfiguration({
    cacheDisabled: true,
  });

  // If the page which is reloaded is not found, this will likely cause
  // reloadTopLevelTarget to not return so let not wait for it.
  if (waitForLoad) {
    await toolbox.commands.targetCommand.reloadTopLevelTarget();
  } else {
    toolbox.commands.targetCommand.reloadTopLevelTarget();
  }
}

async function enableCacheAndReload(toolbox, waitForLoad) {
  // Disable the cache for any toolbox that it is opened from this point on.
  Services.prefs.setBoolPref("devtools.cache.disabled", false);

  await toolbox.commands.targetConfigurationCommand.updateConfiguration({
    cacheDisabled: false,
  });

  // If the page which is reloaded is not found, this will likely cause
  // reloadTopLevelTarget to not return so let not wait for it.
  if (waitForLoad) {
    await toolbox.commands.targetCommand.reloadTopLevelTarget();
  } else {
    toolbox.commands.targetCommand.reloadTopLevelTarget();
  }
}

/**
 * Wait for 2 markers during document load.
 */
function waitForTimelineMarkers(monitor) {
  return new Promise(resolve => {
    const markers = [];

    function handleTimelineEvent(marker) {
      info(`Got marker: ${marker.name}`);
      markers.push(marker);
      if (markers.length == 2) {
        monitor.panelWin.api.off(
          TEST_EVENTS.TIMELINE_EVENT,
          handleTimelineEvent
        );
        info("Got two timeline markers, done waiting");
        resolve(markers);
      }
    }

    monitor.panelWin.api.on(TEST_EVENTS.TIMELINE_EVENT, handleTimelineEvent);
  });
}

let finishedQueue = {};
const updatingTypes = [
  "NetMonitor:NetworkEventUpdating:RequestCookies",
  "NetMonitor:NetworkEventUpdating:ResponseCookies",
  "NetMonitor:NetworkEventUpdating:RequestHeaders",
  "NetMonitor:NetworkEventUpdating:ResponseHeaders",
  "NetMonitor:NetworkEventUpdating:RequestPostData",
  "NetMonitor:NetworkEventUpdating:ResponseContent",
  "NetMonitor:NetworkEventUpdating:SecurityInfo",
  "NetMonitor:NetworkEventUpdating:EventTimings",
];
const updatedTypes = [
  "NetMonitor:NetworkEventUpdated:RequestCookies",
  "NetMonitor:NetworkEventUpdated:ResponseCookies",
  "NetMonitor:NetworkEventUpdated:RequestHeaders",
  "NetMonitor:NetworkEventUpdated:ResponseHeaders",
  "NetMonitor:NetworkEventUpdated:RequestPostData",
  "NetMonitor:NetworkEventUpdated:ResponseContent",
  "NetMonitor:NetworkEventUpdated:SecurityInfo",
  "NetMonitor:NetworkEventUpdated:EventTimings",
];

// Start collecting all networkEventUpdate event when panel is opened.
// removeTab() should be called once all corresponded RECEIVED_* events finished.
function startNetworkEventUpdateObserver(panelWin) {
  updatingTypes.forEach(type =>
    panelWin.api.on(type, actor => {
      const key = actor + "-" + updatedTypes[updatingTypes.indexOf(type)];
      finishedQueue[key] = finishedQueue[key] ? finishedQueue[key] + 1 : 1;
    })
  );

  updatedTypes.forEach(type =>
    panelWin.api.on(type, payload => {
      const key = payload.from + "-" + type;
      finishedQueue[key] = finishedQueue[key] ? finishedQueue[key] - 1 : -1;
    })
  );

  panelWin.api.on("clear-network-resources", () => {
    finishedQueue = {};
  });
}

async function waitForAllNetworkUpdateEvents() {
  function checkNetworkEventUpdateState() {
    for (const key in finishedQueue) {
      if (finishedQueue[key] > 0) {
        return false;
      }
    }
    return true;
  }
  info("Wait for completion of all NetworkUpdateEvents packets...");
  await waitUntil(() => checkNetworkEventUpdateState());
  finishedQueue = {};
}

function initNetMonitor(
  url,
  {
    requestCount,
    expectedEventTimings,
    waitForLoad = true,
    enableCache = false,
    openInPrivateWindow = false,
  }
) {
  info("Initializing a network monitor pane.");

  if (!requestCount && !enableCache) {
    ok(
      false,
      "initNetMonitor should be given a number of requests the page will perform"
    );
  }

  return (async function () {
    let tab = null;
    let privateWindow = null;

    if (openInPrivateWindow) {
      privateWindow = await BrowserTestUtils.openNewBrowserWindow({
        private: true,
      });
      ok(
        PrivateBrowsingUtils.isContentWindowPrivate(privateWindow),
        "window is private"
      );
      tab = BrowserTestUtils.addTab(privateWindow.gBrowser, url);
    } else {
      tab = await addTab(url, { waitForLoad });
    }

    info("Net tab added successfully: " + url);

    const toolbox = await gDevTools.showToolboxForTab(tab, {
      toolId: "netmonitor",
    });
    info("Network monitor pane shown successfully.");

    const monitor = toolbox.getCurrentPanel();

    startNetworkEventUpdateObserver(monitor.panelWin);

    if (!enableCache) {
      info("Disabling cache and reloading page.");

      const allComplete = [];
      allComplete.push(
        waitForNetworkEvents(monitor, requestCount, {
          expectedEventTimings,
        })
      );

      if (waitForLoad) {
        allComplete.push(waitForTimelineMarkers(monitor));
      }
      await disableCacheAndReload(toolbox, waitForLoad);
      await Promise.all(allComplete);
      await clearNetworkEvents(monitor);
    } else if (Services.prefs.getBoolPref("devtools.cache.disabled")) {
      info("Enabling cache and reloading page.");

      const allComplete = [];
      allComplete.push(
        waitForNetworkEvents(monitor, requestCount, {
          expectedEventTimings,
        })
      );

      if (waitForLoad) {
        allComplete.push(waitForTimelineMarkers(monitor));
      }
      await enableCacheAndReload(toolbox, waitForLoad);
      await Promise.all(allComplete);
      await clearNetworkEvents(monitor);
    }

    return { tab, monitor, toolbox, privateWindow };
  })();
}

function restartNetMonitor(monitor, { requestCount }) {
  info("Restarting the specified network monitor.");

  return (async function () {
    const tab = monitor.commands.descriptorFront.localTab;
    const url = tab.linkedBrowser.currentURI.spec;

    await waitForAllNetworkUpdateEvents();
    info("All pending requests finished.");

    const onDestroyed = monitor.once("destroyed");
    await removeTab(tab);
    await onDestroyed;

    return initNetMonitor(url, { requestCount });
  })();
}

/**
 * Clears the network requests in the UI
 * @param {Object} monitor
 *         The netmonitor instance used for retrieving a context menu element.
 */
async function clearNetworkEvents(monitor) {
  const { store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  await waitForAllNetworkUpdateEvents();

  info("Clearing the network requests in the UI");
  store.dispatch(Actions.clearRequests({ isExplicitClear: true }));
}

function teardown(monitor, privateWindow) {
  info("Destroying the specified network monitor.");

  return (async function () {
    const tab = monitor.commands.descriptorFront.localTab;

    await waitForAllNetworkUpdateEvents();
    info("All pending requests finished.");

    await monitor.toolbox.destroy();
    await removeTab(tab);

    if (privateWindow) {
      const closed = BrowserTestUtils.windowClosed(privateWindow);
      privateWindow.BrowserCommands.tryToCloseWindow();
      await closed;
    }
  })();
}

/**
 * Wait for the request(s) to be fully notified to the frontend.
 *
 * @param {Object} monitor
 *        The netmonitor instance used for retrieving a context menu element.
 * @param {Number} getRequests
 *        The number of request to wait for
 * @param {Object} options (optional)
 *        - expectedEventTimings {Number} Number of EVENT_TIMINGS events to wait for.
 *        In case of filtering, we get less of such events.
 */
function waitForNetworkEvents(monitor, getRequests, options = {}) {
  return new Promise(resolve => {
    const panel = monitor.panelWin;
    let networkEvent = 0;
    let payloadReady = 0;
    let eventTimings = 0;

    // Use a set to monitor blocked events, because a network resource might
    // only receive its blockedReason in onPayloadReady.
    let nonBlockedNetworkEvents = new Set();

    function onNetworkEvent(resource) {
      networkEvent++;
      if (!resource.blockedReason) {
        nonBlockedNetworkEvents.add(resource.actor);
      }
      maybeResolve(TEST_EVENTS.NETWORK_EVENT, resource.actor);
    }

    function onPayloadReady(resource) {
      payloadReady++;
      if (resource.blockedReason) {
        nonBlockedNetworkEvents.delete(resource.actor);
      }
      maybeResolve(EVENTS.PAYLOAD_READY, resource.actor);
    }

    function onEventTimings(response) {
      eventTimings++;
      maybeResolve(EVENTS.RECEIVED_EVENT_TIMINGS, response.from);
    }

    function onClearNetworkResources() {
      // Reset all counters.
      networkEvent = 0;
      nonBlockedNetworkEvents = new Set();
      payloadReady = 0;
      eventTimings = 0;
    }

    function maybeResolve(event, actor) {
      const { document } = monitor.panelWin;
      // Wait until networkEvent, payloadReady and event timings finish for each request.
      // The UI won't fetch timings when:
      // * hidden in background,
      // * for any blocked request,
      let expectedEventTimings =
        document.visibilityState == "hidden" ? 0 : nonBlockedNetworkEvents.size;
      let expectedPayloadReady = getRequests;
      // Typically ignore this option if it is undefined or null
      if (typeof options?.expectedEventTimings == "number") {
        expectedEventTimings = options.expectedEventTimings;
      }
      if (typeof options?.expectedPayloadReady == "number") {
        expectedPayloadReady = options.expectedPayloadReady;
      }
      info(
        "> Network event progress: " +
          "NetworkEvent: " +
          networkEvent +
          "/" +
          getRequests +
          ", " +
          "PayloadReady: " +
          payloadReady +
          "/" +
          expectedPayloadReady +
          ", " +
          "EventTimings: " +
          eventTimings +
          "/" +
          expectedEventTimings +
          ", " +
          "got " +
          event +
          " for " +
          actor
      );

      if (
        networkEvent >= getRequests &&
        payloadReady >= expectedPayloadReady &&
        eventTimings >= expectedEventTimings
      ) {
        panel.api.off(TEST_EVENTS.NETWORK_EVENT, onNetworkEvent);
        panel.api.off(EVENTS.PAYLOAD_READY, onPayloadReady);
        panel.api.off(EVENTS.RECEIVED_EVENT_TIMINGS, onEventTimings);
        panel.api.off("clear-network-resources", onClearNetworkResources);
        executeSoon(resolve);
      }
    }

    panel.api.on(TEST_EVENTS.NETWORK_EVENT, onNetworkEvent);
    panel.api.on(EVENTS.PAYLOAD_READY, onPayloadReady);
    panel.api.on(EVENTS.RECEIVED_EVENT_TIMINGS, onEventTimings);
    panel.api.on("clear-network-resources", onClearNetworkResources);
  });
}

function verifyRequestItemTarget(
  document,
  requestList,
  requestItem,
  method,
  url,
  data = {}
) {
  info("> Verifying: " + method + " " + url + " " + data.toSource());

  const visibleIndex = requestList.findIndex(
    needle => needle.id === requestItem.id
  );

  isnot(visibleIndex, -1, "The requestItem exists");
  info("Visible index of item: " + visibleIndex);

  const {
    fuzzyUrl,
    status,
    statusText,
    cause,
    type,
    fullMimeType,
    transferred,
    size,
    time,
    displayedStatus,
  } = data;

  const target = document.querySelectorAll(".request-list-item")[visibleIndex];

  // Bug 1414981 - Request URL should not show #hash
  const unicodeUrl = getUnicodeUrl(url.split("#")[0]);
  const ORIGINAL_FILE_URL = L10N.getFormatStr(
    "netRequest.originalFileURL.tooltip",
    url
  );
  const DECODED_FILE_URL = L10N.getFormatStr(
    "netRequest.decodedFileURL.tooltip",
    unicodeUrl
  );
  const fileToolTip =
    url === unicodeUrl ? url : ORIGINAL_FILE_URL + "\n\n" + DECODED_FILE_URL;
  const requestedFile = requestItem.urlDetails.baseNameWithQuery;
  const host = getUnicodeHostname(getUrlHost(url));
  const scheme = getUrlScheme(url);
  const {
    remoteAddress,
    remotePort,
    totalTime,
    eventTimings = { timings: {} },
  } = requestItem;
  const formattedIPPort = getFormattedIPAndPort(remoteAddress, remotePort);
  const remoteIP = remoteAddress ? `${formattedIPPort}` : "unknown";
  // TODO Bug 1959359: timing columns duration and latency use a custom formatting for now for undefined/NaN values
  const duration =
    totalTime === undefined || isNaN(totalTime)
      ? ""
      : getFormattedTime(totalTime);
  const latency =
    eventTimings.timings.wait === undefined || isNaN(eventTimings.timings.wait)
      ? ""
      : getFormattedTime(eventTimings.timings.wait);
  const protocol = getFormattedProtocol(requestItem);

  if (fuzzyUrl) {
    ok(
      requestItem.method.startsWith(method),
      "The attached method is correct."
    );
    ok(requestItem.url.startsWith(url), "The attached url is correct.");
  } else {
    is(requestItem.method, method, "The attached method is correct.");
    is(requestItem.url, url.split("#")[0], "The attached url is correct.");
  }

  is(
    target.querySelector(".requests-list-method").textContent,
    method,
    "The displayed method is correct."
  );

  if (fuzzyUrl) {
    ok(
      target
        .querySelector(".requests-list-file")
        .textContent.startsWith(requestedFile),
      "The displayed file is correct."
    );
    ok(
      target
        .querySelector(".requests-list-file")
        .getAttribute("title")
        .startsWith(fileToolTip),
      "The tooltip file is correct."
    );
  } else {
    is(
      target.querySelector(".requests-list-file").textContent,
      requestedFile,
      "The displayed file is correct."
    );
    is(
      target.querySelector(".requests-list-file").getAttribute("title"),
      fileToolTip,
      "The tooltip file is correct."
    );
  }

  is(
    target.querySelector(".requests-list-protocol").textContent,
    protocol,
    "The displayed protocol is correct."
  );

  is(
    target.querySelector(".requests-list-protocol").getAttribute("title"),
    protocol,
    "The tooltip protocol is correct."
  );

  is(
    target.querySelector(".requests-list-domain").textContent,
    host,
    "The displayed domain is correct."
  );

  const domainTooltip =
    host + (remoteAddress ? " (" + formattedIPPort + ")" : "");
  is(
    target.querySelector(".requests-list-domain").getAttribute("title"),
    domainTooltip,
    "The tooltip domain is correct."
  );

  is(
    target.querySelector(".requests-list-remoteip").textContent,
    remoteIP,
    "The displayed remote IP is correct."
  );

  is(
    target.querySelector(".requests-list-remoteip").getAttribute("title"),
    remoteIP,
    "The tooltip remote IP is correct."
  );

  is(
    target.querySelector(".requests-list-scheme").textContent,
    scheme,
    "The displayed scheme is correct."
  );

  is(
    target.querySelector(".requests-list-scheme").getAttribute("title"),
    scheme,
    "The tooltip scheme is correct."
  );

  is(
    target.querySelector(".requests-list-duration-time").textContent,
    duration,
    "The displayed duration is correct."
  );

  is(
    target.querySelector(".requests-list-duration-time").getAttribute("title"),
    duration,
    "The tooltip duration is correct."
  );

  is(
    target.querySelector(".requests-list-latency-time").textContent,
    latency,
    "The displayed latency is correct."
  );

  is(
    target.querySelector(".requests-list-latency-time").getAttribute("title"),
    latency,
    "The tooltip latency is correct."
  );

  if (status !== undefined) {
    const value = target
      .querySelector(".requests-list-status-code")
      .getAttribute("data-status-code");
    const codeValue = target.querySelector(
      ".requests-list-status-code"
    ).textContent;
    const tooltip = target
      .querySelector(".requests-list-status-code")
      .getAttribute("title");
    info("Displayed status: " + value);
    info("Displayed code: " + codeValue);
    info("Tooltip status: " + tooltip);
    is(
      `${value}`,
      displayedStatus ? `${displayedStatus}` : `${status}`,
      "The displayed status is correct."
    );
    is(`${codeValue}`, `${status}`, "The displayed status code is correct.");
    is(tooltip, status + " " + statusText, "The tooltip status is correct.");
  }
  if (cause !== undefined) {
    const value = Array.from(
      target.querySelector(".requests-list-initiator").childNodes
    )
      .filter(node => node.nodeType === Node.ELEMENT_NODE)
      .map(({ textContent }) => textContent)
      .join("");
    const tooltip = target
      .querySelector(".requests-list-initiator")
      .getAttribute("title");
    info("Displayed cause: " + value);
    info("Tooltip cause: " + tooltip);
    ok(value.includes(cause.type), "The displayed cause is correct.");
    ok(tooltip.includes(cause.type), "The tooltip cause is correct.");
  }
  if (type !== undefined) {
    const value = target.querySelector(".requests-list-type").textContent;
    let tooltip = target
      .querySelector(".requests-list-type")
      .getAttribute("title");
    info("Displayed type: " + value);
    info("Tooltip type: " + tooltip);
    is(value, type, "The displayed type is correct.");
    if (Object.is(tooltip, null)) {
      tooltip = undefined;
    }
    is(tooltip, fullMimeType, "The tooltip type is correct.");
  }
  if (transferred !== undefined) {
    const value = target.querySelector(
      ".requests-list-transferred"
    ).textContent;
    const tooltip = target
      .querySelector(".requests-list-transferred")
      .getAttribute("title");
    info("Displayed transferred size: " + value);
    info("Tooltip transferred size: " + tooltip);
    is(value, transferred, "The displayed transferred size is correct.");
    is(tooltip, transferred, "The tooltip transferred size is correct.");
  }
  if (size !== undefined) {
    const value = target.querySelector(".requests-list-size").textContent;
    const tooltip = target
      .querySelector(".requests-list-size")
      .getAttribute("title");
    info("Displayed size: " + value);
    info("Tooltip size: " + tooltip);
    is(value, size, "The displayed size is correct.");
    is(tooltip, size, "The tooltip size is correct.");
  }
  if (time !== undefined) {
    const value = target.querySelector(
      ".requests-list-timings-total"
    ).textContent;
    const tooltip = target
      .querySelector(".requests-list-timings-total")
      .getAttribute("title");
    info("Displayed time: " + value);
    info("Tooltip time: " + tooltip);
    Assert.greaterOrEqual(
      ~~value.match(/[0-9]+/),
      0,
      "The displayed time is correct."
    );
    Assert.greaterOrEqual(
      ~~tooltip.match(/[0-9]+/),
      0,
      "The tooltip time is correct."
    );
  }

  if (visibleIndex !== -1) {
    if (visibleIndex % 2 === 0) {
      ok(target.classList.contains("even"), "Item should have 'even' class.");
      ok(!target.classList.contains("odd"), "Item shouldn't have 'odd' class.");
    } else {
      ok(
        !target.classList.contains("even"),
        "Item shouldn't have 'even' class."
      );
      ok(target.classList.contains("odd"), "Item should have 'odd' class.");
    }
  }
}

/**
 * Tests if a button for a filter of given type is the only one checked.
 *
 * @param string filterType
 *        The type of the filter that should be the only one checked.
 */
function testFilterButtons(monitor, filterType) {
  const doc = monitor.panelWin.document;
  const target = doc.querySelector(
    ".requests-list-filter-" + filterType + "-button"
  );
  ok(target, `Filter button '${filterType}' was found`);
  const buttons = [
    ...doc.querySelectorAll(".requests-list-filter-buttons button"),
  ];
  ok(!!buttons.length, "More than zero filter buttons were found");

  // Only target should be checked.
  const checkStatus = buttons.map(button => (button == target ? 1 : 0));
  testFilterButtonsCustom(monitor, checkStatus);
}

/**
 * Tests if filter buttons have 'checked' attributes set correctly.
 *
 * @param array aIsChecked
 *        An array specifying if a button at given index should have a
 *        'checked' attribute. For example, if the third item of the array
 *        evaluates to true, the third button should be checked.
 */
function testFilterButtonsCustom(monitor, isChecked) {
  const doc = monitor.panelWin.document;
  const buttons = doc.querySelectorAll(".requests-list-filter-buttons button");
  for (let i = 0; i < isChecked.length; i++) {
    const button = buttons[i];
    if (isChecked[i]) {
      is(
        button.getAttribute("aria-pressed"),
        "true",
        "The " + button.id + " button should set 'aria-pressed' = true."
      );
    } else {
      is(
        button.getAttribute("aria-pressed"),
        "false",
        "The " + button.id + " button should set 'aria-pressed' = false."
      );
    }
  }
}

/**
 * Performs a single XMLHttpRequest and returns a promise that resolves once
 * the request has loaded.
 *
 * @param Object data
 *        { method: the request method (default: "GET"),
 *          url: the url to request (default: content.location.href),
 *          body: the request body to send (default: ""),
 *          nocache: append an unique token to the query string (default: true),
 *          requestHeaders: set request headers (default: none)
 *        }
 *
 * @return Promise A promise that's resolved with object
 *         { status: XMLHttpRequest.status,
 *           response: XMLHttpRequest.response }
 *
 */
function promiseXHR(data) {
  return new Promise(resolve => {
    const xhr = new content.XMLHttpRequest();

    const method = data.method || "GET";
    let url = data.url || content.location.href;
    const body = data.body || "";

    if (data.nocache) {
      url += "?devtools-cachebust=" + Math.random();
    }

    xhr.addEventListener(
      "loadend",
      function () {
        resolve({ status: xhr.status, response: xhr.response });
      },
      { once: true }
    );

    xhr.open(method, url);

    // Set request headers
    if (data.requestHeaders) {
      data.requestHeaders.forEach(header => {
        xhr.setRequestHeader(header.name, header.value);
      });
    }

    xhr.send(body);
  });
}

/**
 * Performs a single websocket request and returns a promise that resolves once
 * the request has loaded.
 *
 * @param Object data
 *        { url: the url to request (default: content.location.href),
 *          nocache: append an unique token to the query string (default: true),
 *        }
 *
 * @return Promise A promise that's resolved with object
 *         { status: websocket status(101),
 *           response: empty string }
 *
 */
function promiseWS(data) {
  return new Promise(resolve => {
    let url = data.url;

    if (data.nocache) {
      url += "?devtools-cachebust=" + Math.random();
    }

    /* Create websocket instance */
    const socket = new content.WebSocket(url);

    /* Since we only use HTTP server to mock websocket, so just ignore the error */
    socket.onclose = () => {
      socket.close();
      resolve({
        status: 101,
        response: "",
      });
    };

    socket.onerror = () => {
      socket.close();
      resolve({
        status: 101,
        response: "",
      });
    };
  });
}

/**
 * Perform the specified requests in the context of the page content.
 *
 * @param Array requests
 *        An array of objects specifying the requests to perform. See
 *        shared/test/frame-script-utils.js for more information.
 *
 * @return A promise that resolves once the requests complete.
 */
async function performRequestsInContent(requests) {
  if (!Array.isArray(requests)) {
    requests = [requests];
  }

  const responses = [];

  info("Performing requests in the context of the content.");

  for (const request of requests) {
    const requestFn = request.ws ? promiseWS : promiseXHR;
    const response = await SpecialPowers.spawn(
      gBrowser.selectedBrowser,
      [request],
      requestFn
    );
    responses.push(response);
  }
}

function testColumnsAlignment(headers, requestList) {
  const firstRequestLine = requestList.childNodes[0];

  // Find number of columns
  const numberOfColumns = headers.childElementCount;
  for (let i = 0; i < numberOfColumns; i++) {
    const headerColumn = headers.childNodes[i];
    const requestColumn = firstRequestLine.childNodes[i];
    is(
      headerColumn.getBoundingClientRect().left,
      requestColumn.getBoundingClientRect().left,
      "Headers for columns number " + i + " are aligned."
    );
  }
}

async function hideColumn(monitor, column) {
  const { document } = monitor.panelWin;

  info(`Clicking context-menu item for ${column}`);
  EventUtils.sendMouseEvent(
    { type: "contextmenu" },
    document.querySelector(".requests-list-headers")
  );

  const onHeaderRemoved = waitForDOM(
    document,
    `#requests-list-${column}-button`,
    0
  );
  await selectContextMenuItem(monitor, `request-list-header-${column}-toggle`);
  await onHeaderRemoved;

  ok(
    !document.querySelector(`#requests-list-${column}-button`),
    `Column ${column} should be hidden`
  );
}

async function showColumn(monitor, column) {
  const { document } = monitor.panelWin;

  info(`Clicking context-menu item for ${column}`);
  EventUtils.sendMouseEvent(
    { type: "contextmenu" },
    document.querySelector(".requests-list-headers")
  );

  const onHeaderAdded = waitForDOM(
    document,
    `#requests-list-${column}-button`,
    1
  );
  await selectContextMenuItem(monitor, `request-list-header-${column}-toggle`);
  await onHeaderAdded;

  ok(
    document.querySelector(`#requests-list-${column}-button`),
    `Column ${column} should be visible`
  );
}

/**
 * Select a request and switch to its response panel.
 *
 * @param {Number} index The request index to be selected
 */
async function selectIndexAndWaitForSourceEditor(monitor, index) {
  const { document } = monitor.panelWin;
  const onResponseContent = monitor.panelWin.api.once(
    TEST_EVENTS.RECEIVED_RESPONSE_CONTENT
  );
  // Select the request first, as it may try to fetch whatever is the current request's
  // responseContent if we select the ResponseTab first.
  EventUtils.sendMouseEvent(
    { type: "mousedown" },
    document.querySelectorAll(".request-list-item")[index]
  );
  // We may already be on the ResponseTab, so only select it if needed.
  const editor = document.querySelector("#response-panel .CodeMirror-code");
  if (!editor) {
    const waitDOM = waitForDOM(document, "#response-panel .CodeMirror-code");
    document.querySelector("#response-tab").click();
    await waitDOM;
  }
  await onResponseContent;
}

/**
 * Helper function for executing XHRs on a test page.
 *
 * @param {Number} count Number of requests to be executed.
 */
async function performRequests(monitor, tab, count) {
  const wait = waitForNetworkEvents(monitor, count);
  await ContentTask.spawn(tab.linkedBrowser, count, requestCount => {
    content.wrappedJSObject.performRequests(requestCount);
  });
  await wait;
}

/**
 * Helper function for retrieving `.CodeMirror` content
 */
function getCodeMirrorValue(monitor) {
  const { document } = monitor.panelWin;
  return document.querySelector(".CodeMirror").CodeMirror.getValue();
}

/**
 * Helper function opening the options menu
 */
function openSettingsMenu(monitor) {
  const { document } = monitor.panelWin;
  document.querySelector(".netmonitor-settings-menu-button").click();
}

function clickSettingsMenuItem(monitor, itemKey) {
  openSettingsMenu(monitor);
  const node = getSettingsMenuItem(monitor, itemKey);
  node.click();
}

function getSettingsMenuItem(monitor, itemKey) {
  // The settings menu is injected into the toolbox document,
  // so we must use the panelWin parent to query for items
  const { parent } = monitor.panelWin;
  const { document } = parent;

  return document.querySelector(SETTINGS_MENU_ITEMS[itemKey]);
}

/**
 * Wait for lazy fields to be loaded in a request.
 *
 * @param Object Store redux store containing request list.
 * @param array fields array of strings which contain field names to be checked
 * on the request.
 */
function waitForRequestData(store, fields, id) {
  return waitUntil(() => {
    let item;
    if (id) {
      item = getRequestById(store.getState(), id);
    } else {
      item = getSortedRequests(store.getState())[0];
    }
    if (!item) {
      return false;
    }
    for (const field of fields) {
      if (!item[field]) {
        return false;
      }
    }
    return true;
  });
}

// Telemetry

/**
 * Helper for verifying telemetry event.
 *
 * @param Object expectedEvent object representing expected event data.
 * @param Object query fields specifying category, method and object
 *                     of the target telemetry event.
 */
function checkTelemetryEvent(expectedEvent, query) {
  const events = queryTelemetryEvents(query);
  is(events.length, 1, "There was only 1 event logged");

  const [event] = events;
  Assert.greater(
    Number(event.session_id),
    0,
    "There is a valid session_id in the logged event"
  );

  const f = e => JSON.stringify(e, null, 2);
  is(
    f(event),
    f({
      ...expectedEvent,
      session_id: event.session_id,
    }),
    "The event has the expected data"
  );
}

function queryTelemetryEvents(query) {
  const ALL_CHANNELS = Ci.nsITelemetry.DATASET_ALL_CHANNELS;
  const snapshot = Services.telemetry.snapshotEvents(ALL_CHANNELS, true);
  const category = query.category || "devtools.main";
  const object = query.object || "netmonitor";

  const filtersChangedEvents = snapshot.parent.filter(
    event =>
      event[1] === category && event[2] === query.method && event[3] === object
  );

  // Return the `extra` field (which is event[5]e).
  return filtersChangedEvents.map(event => event[5]);
}
/**
 * Check that the provided requests match the requests displayed in the netmonitor.
 *
 * @param {array} requests
 *     The expected requests.
 * @param {object} monitor
 *     The netmonitor instance.
 * @param {object=} options
 * @param {boolean} allowDifferentOrder
 *     When set to true, requests are allowed to be in a different order in the
 *     netmonitor than in the expected requests array. Defaults to false.
 */
function validateRequests(requests, monitor, options = {}) {
  const { allowDifferentOrder } = options;
  const { document, store, windowRequire } = monitor.panelWin;

  const { getDisplayedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );
  const sortedRequests = getSortedRequests(store.getState());

  requests.forEach((spec, i) => {
    const { method, url, causeType, causeUri, stack } = spec;

    let requestItem;
    if (allowDifferentOrder) {
      requestItem = sortedRequests.find(r => r.url === url);
    } else {
      requestItem = sortedRequests[i];
    }

    verifyRequestItemTarget(
      document,
      getDisplayedRequests(store.getState()),
      requestItem,
      method,
      url,
      { cause: { type: causeType, loadingDocumentUri: causeUri } }
    );

    const { stacktrace } = requestItem;
    const stackLen = stacktrace ? stacktrace.length : 0;

    if (stack) {
      ok(stacktrace, `Request #${i} has a stacktrace`);
      Assert.greater(
        stackLen,
        0,
        `Request #${i} (${causeType}) has a stacktrace with ${stackLen} items`
      );

      // if "stack" is array, check the details about the top stack frames
      if (Array.isArray(stack)) {
        stack.forEach((frame, j) => {
          // If the `fn` is "*", it means the request is triggered from chrome
          // resources, e.g. `resource:///modules/XX.jsm`, so we skip checking
          // the function name for now (bug 1280266).
          if (frame.file.startsWith("resource:///")) {
            todo(false, "Requests from chrome resource should not be included");
          } else {
            let value = stacktrace[j].functionName;
            if (Object.is(value, null)) {
              value = undefined;
            }
            is(
              value,
              frame.fn,
              `Request #${i} has the correct function on JS stack frame #${j}`
            );
            is(
              stacktrace[j].filename.split("/").pop(),
              frame.file.split("/").pop(),
              `Request #${i} has the correct file on JS stack frame #${j}`
            );
            is(
              stacktrace[j].lineNumber,
              frame.line,
              `Request #${i} has the correct line number on JS stack frame #${j}`
            );
            value = stacktrace[j].asyncCause;
            if (Object.is(value, null)) {
              value = undefined;
            }
            is(
              value,
              frame.asyncCause,
              `Request #${i} has the correct async cause on JS stack frame #${j}`
            );
          }
        });
      }
    } else {
      is(stackLen, 0, `Request #${i} (${causeType}) has an empty stacktrace`);
    }
  });
}

/**
 * @see getNetmonitorContextMenuItem in shared-head.js
 */
function getContextMenuItem(monitor, id) {
  return getNetmonitorContextMenuItem(monitor, id);
}

/**
 * Hides the provided netmonitor context menu
 *
 * @param {XULPopupElement} popup
 *        The popup to hide.
 */
async function hideContextMenu(popup) {
  if (popup.state !== "open") {
    return;
  }
  const onPopupHidden = BrowserTestUtils.waitForEvent(popup, "popuphidden");
  popup.hidePopup();
  await onPopupHidden;
}

/**
 * @see selectNetmonitorContextMenuItem in shared-head.js
 */
async function selectContextMenuItem(monitor, id) {
  return selectNetmonitorContextMenuItem(monitor, id);
}

/**
 * Wait for DOM being in specific state. But, do not wait
 * for change if it's in the expected state already.
 */
async function waitForDOMIfNeeded(target, selector, expectedLength = 1) {
  return new Promise(resolve => {
    const elements = target.querySelectorAll(selector);
    if (elements.length == expectedLength) {
      resolve(elements);
    } else {
      waitForDOM(target, selector, expectedLength).then(elems => {
        resolve(elems);
      });
    }
  });
}

/**
 * Helper for blocking or unblocking a request via the list item's context menu.
 *
 * @param {Element} element
 *        Target request list item to be right clicked to bring up its context menu.
 * @param {Object} monitor
 *        The netmonitor instance used for retrieving a context menu element.
 * @param {Object} store
 *        The redux store (wait-service middleware required).
 * @param {String} action
 *        The action, block or unblock, to construct a corresponding context menu id.
 */
async function toggleBlockedUrl(element, monitor, store, action = "block") {
  EventUtils.sendMouseEvent({ type: "contextmenu" }, element);
  const contextMenuId = `request-list-context-${action}-url`;
  const onRequestComplete = waitForDispatch(
    store,
    "REQUEST_BLOCKING_UPDATE_COMPLETE"
  );
  await selectContextMenuItem(monitor, contextMenuId);

  info(`Wait for selected request to be ${action}ed`);
  await onRequestComplete;
  info(`Selected request is now ${action}ed`);
}

/**
 * Find and click an element
 *
 * @param {Element} element
 *        Target element to be clicked
 * @param {Object} monitor
 *        The netmonitor instance used for retrieving the window.
 */

function clickElement(element, monitor) {
  EventUtils.synthesizeMouseAtCenter(element, {}, monitor.panelWin);
}

/**
 * Register a listener to be notified when a favicon finished loading and
 * dispatch a "devtools:test:favicon" event to the favicon's link element.
 *
 * @param {Browser} browser
 *        Target browser to observe the favicon load.
 */
function registerFaviconNotifier(browser) {
  const listener = async name => {
    if (name == "SetIcon" || name == "SetFailedIcon") {
      await SpecialPowers.spawn(browser, [], async () => {
        content.document
          .querySelector("link[rel='icon']")
          .dispatchEvent(new content.CustomEvent("devtools:test:favicon"));
      });
      LinkHandlerParent.removeListenerForTests(listener);
    }
  };
  LinkHandlerParent.addListenerForTests(listener);
}

/**
 * Predicates used when sorting items.
 *
 * @param object first
 *        The first item used in the comparison.
 * @param object second
 *        The second item used in the comparison.
 * @return number
 *         <0 to sort first to a lower index than second
 *         =0 to leave first and second unchanged with respect to each other
 *         >0 to sort second to a lower index than first
 */

function compareValues(first, second) {
  if (first === second) {
    return 0;
  }
  return first > second ? 1 : -1;
}

/**
 * Click on the "Response" tab to open "Response" panel in the sidebar.
 * @param {Document} doc
 *        Network panel document.
 * @param {String} name
 *        Network panel sidebar tab name.
 */
const clickOnSidebarTab = (doc, name) => {
  AccessibilityUtils.setEnv({
    // Keyboard accessibility is handled on the sidebar tabs container level
    // (nav). Users can use arrow keys to navigate between and select tabs.
    nonNegativeTabIndexRule: false,
  });
  EventUtils.sendMouseEvent(
    { type: "click" },
    doc.querySelector(`#${name}-tab`)
  );
  AccessibilityUtils.resetEnv();
};

/**
 * Add a new blocked request URL pattern. The request blocking sidepanel should
 * already be opened.
 *
 * @param {string} pattern
 *     The URL pattern to add to block requests.
 * @param {Object} monitor
 *     The netmonitor instance.
 */
async function addBlockedRequest(pattern, monitor) {
  info("Add a blocked request for the URL pattern " + pattern);
  const doc = monitor.panelWin.document;

  const addRequestForm = await waitFor(() =>
    doc.querySelector(
      "#network-action-bar-blocked-panel .request-blocking-add-form"
    )
  );
  ok(!!addRequestForm, "The request blocking side panel is not available");

  info("Wait for the add input to get focus");
  await waitFor(() =>
    addRequestForm.querySelector("input.devtools-searchinput:focus")
  );

  typeInNetmonitor(pattern, monitor);
  EventUtils.synthesizeKey("KEY_Enter");
}

/**
 * Check if the provided .request-list-item element corresponds to a blocked
 * request.
 *
 * @param {Element}
 *     The request's DOM element.
 * @returns {boolean}
 *     True if the request is displayed as blocked, false otherwise.
 */
function checkRequestListItemBlocked(item) {
  return item.className.includes("blocked");
}

/**
 * Type the provided string the netmonitor window. The correct input should be
 * focused prior to using this helper.
 *
 * @param {string} string
 *     The string to type.
 * @param {Object} monitor
 *     The netmonitor instance used to type the string.
 */
function typeInNetmonitor(string, monitor) {
  for (const ch of string) {
    EventUtils.synthesizeKey(ch, {}, monitor.panelWin);
  }
}

/**
 * Opens/ closes the URL preview in the headers side panel
 *
 * @param {Boolean} shouldExpand
 * @param {NetMonitorPanel} monitor
 * @returns
 */
async function toggleUrlPreview(shouldExpand, monitor) {
  const { document } = monitor.panelWin;
  const wait = waitUntil(() => {
    const rowSize = document.querySelectorAll(
      "#headers-panel .url-preview tr.treeRow"
    ).length;
    return shouldExpand ? rowSize > 1 : rowSize == 1;
  });

  clickElement(
    document.querySelector(
      "#headers-panel .url-preview tr:first-child span.treeIcon.theme-twisty"
    ),
    monitor
  );
  return wait;
}

/**
 * Wait for the eager evaluated result from the split console
 * @param {Object} hud
 * @param {String} text - expected evaluation result
 */
async function waitForEagerEvaluationResult(hud, text) {
  await waitUntil(() => {
    const elem = hud.ui.outputNode.querySelector(".eager-evaluation-result");
    if (elem) {
      if (text instanceof RegExp) {
        return text.test(elem.innerText);
      }
      return elem.innerText == text;
    }
    return false;
  });
  ok(true, `Got eager evaluation result ${text}`);
}

/**
 * Assert the contents of the filter urls autocomplete box
 *
 * @param {Array} expected
 * @param {Object} document
 */
function testAutocompleteContents(expected, document) {
  expected.forEach(function (item, i) {
    is(
      document.querySelector(
        `.devtools-autocomplete-listbox .autocomplete-item:nth-child(${i + 1})`
      ).textContent,
      item,
      `${expected[i]} found`
    );
  });
}

/**
 * Check if a valid numerical size is displayed in the request column for the
 * provided request.
 *
 * @param {Element} request
 *     A request element from the netmonitor requests list.
 * @return {boolean}
 *     True if the size column contains a valid size, false otherwise.
 *
 */
function hasValidSize(request) {
  const VALID_SIZE_RE = /^\d+(\.\d+)? \w+/;
  return VALID_SIZE_RE.test(
    request.querySelector(".requests-list-size").innerText
  );
}

function getThrottleProfileItem(monitor, profileId) {
  const toolboxDoc = monitor.toolbox.doc;

  const popup = toolboxDoc.querySelector("#network-throttling-menu");
  const menuItems = [...popup.querySelectorAll(".menuitem > .command")];
  return menuItems.find(menuItem => menuItem.id == profileId);
}

async function selectThrottle(monitor, profileId) {
  const panelDoc = monitor.panelWin.document;
  const toolboxDoc = monitor.toolbox.doc;

  info("Opening the throttling menu");

  const onShown = BrowserTestUtils.waitForPopupEvent(toolboxDoc, "shown");
  panelDoc.getElementById("network-throttling").click();

  info("Waiting for the throttling menu to be displayed");
  await onShown;

  const profileItem = getThrottleProfileItem(monitor, profileId);
  ok(profileItem, "Found a profile throttling menu item for id " + profileId);

  info(`Selecting the '${profileId}' profile`);
  profileItem.click();

  info(`Waiting for the '${profileId}' profile to be applied`);
  await monitor.panelWin.api.once(TEST_EVENTS.THROTTLING_CHANGED);
}

/**
 * Resize a netmonitor column.
 *
 * @param {Element} columnHeader
 * @param {number} newPercent
 * @param {number} parentWidth
 * @param {string} dir
 */
function resizeColumn(columnHeader, newPercent, parentWidth, dir = "ltr") {
  const newWidthInPixels = (newPercent * parentWidth) / 100;
  const win = columnHeader.ownerDocument.defaultView;
  const currentWidth = columnHeader.getBoundingClientRect().width;
  const mouseDown = dir === "rtl" ? 0 : currentWidth;
  const mouseMove =
    dir === "rtl" ? currentWidth - newWidthInPixels : newWidthInPixels;

  EventUtils.synthesizeMouse(
    columnHeader,
    mouseDown,
    1,
    { type: "mousedown" },
    win
  );
  EventUtils.synthesizeMouse(
    columnHeader,
    mouseMove,
    1,
    { type: "mousemove" },
    win
  );
  EventUtils.synthesizeMouse(
    columnHeader,
    mouseMove,
    1,
    { type: "mouseup" },
    win
  );
}

/**
 * Resize the waterfall netmonitor column.
 * Uses slightly different logic than for the other columns.
 *
 * @param {Element} columnHeader
 * @param {number} newPercent
 * @param {number} parentWidth
 * @param {string} dir
 */
function resizeWaterfallColumn(
  columnHeader,
  newPercent,
  parentWidth,
  dir = "ltr"
) {
  const newWidthInPixels = (newPercent * parentWidth) / 100;
  const win = columnHeader.ownerDocument.defaultView;
  const mouseDown =
    dir === "rtl"
      ? columnHeader.getBoundingClientRect().right
      : columnHeader.getBoundingClientRect().left;
  const mouseMove =
    dir === "rtl"
      ? mouseDown +
        (newWidthInPixels - columnHeader.getBoundingClientRect().width)
      : mouseDown +
        (columnHeader.getBoundingClientRect().width - newWidthInPixels);

  EventUtils.synthesizeMouse(
    columnHeader.parentElement,
    mouseDown,
    1,
    { type: "mousedown" },
    win
  );
  EventUtils.synthesizeMouse(
    columnHeader.parentElement,
    mouseMove,
    1,
    { type: "mousemove" },
    win
  );
  EventUtils.synthesizeMouse(
    columnHeader.parentElement,
    mouseMove,
    1,
    { type: "mouseup" },
    win
  );
}

function getCurrentVisibleColumns(monitor) {
  const { store, windowRequire } = monitor.panelWin;
  const { getColumns, getVisibleColumns, hasOverride } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );
  const hasOverrideState = hasOverride(monitor.toolbox.store.getState());
  const visibleColumns = getVisibleColumns(
    getColumns(store.getState(), hasOverrideState)
  );

  // getVisibleColumns returns an array of arrays [name, isVisible=true], flatten
  // to return name.
  return visibleColumns.map(([name]) => name);
}

function findRequestByInitiator(document, initiator) {
  for (const request of document.querySelectorAll(".request-list-item")) {
    if (
      request.querySelector(".requests-list-initiator").getAttribute("title") ==
      initiator
    ) {
      return request;
    }
  }
  return null;
}

/**
 * Click on the "save response as" context menu item for the provided request
 * element in the provided netmonitor panel.
 *
 * Resolves when the context menu is closed.
 *
 * @param {object} monitor
 *     The netmonitor instance
 * @param {HTMLElement} request
 *     The request item in the netmonitor table
 */
async function triggerSaveResponseAs(monitor, request) {
  EventUtils.sendMouseEvent({ type: "mousedown" }, request);
  EventUtils.sendMouseEvent({ type: "contextmenu" }, request);

  info("Open the save dialog");
  await selectContextMenuItem(monitor, "request-list-context-save-response-as");
}

/**
 * Wait until the provided path has a non-zero size on the file system.
 *
 * @param {string} path
 *     The path to wait for.
 */
async function waitForFileSavedToDisk(path) {
  info("Wait for the downloaded file to be fully saved to disk: " + path);
  await TestUtils.waitForCondition(async () => {
    if (!(await IOUtils.exists(path))) {
      return false;
    }
    const { size } = await IOUtils.stat(path);
    return size > 0;
  });
}

/**
 * Create a temporary directory to save files for a test.
 * Register a cleanup function to delete the directory after the test.
 *
 * @returns {nsIFile}
 *     The created temporary directory.
 */
function createTemporarySaveDirectory() {
  const saveDir = Services.dirsvc.get("TmpD", Ci.nsIFile);
  saveDir.append("testsavedir");

  if (!saveDir.exists()) {
    saveDir.create(Ci.nsIFile.DIRECTORY_TYPE, 0o755);
  }

  registerCleanupFunction(function () {
    saveDir.remove(true);
  });
  return saveDir;
}
